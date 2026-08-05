#include "TerrainSerializer.hpp"
#include "TerrainFileAccess.hpp"
#include "TerrainLayerSidecar.hpp"
#include "TerrainSaveFaultInjection.hpp"
#include "TerrainSaveJournal.hpp"
#include "../print/Log.hpp"
#include "../resource/AtomicFileReplace.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <numeric>
#include <shared_mutex>

namespace terrain
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr size_t COMPACT_COPY_CHUNK_BYTES = 1u << 20;

        fs::path terrainTempPath(const fs::path& terrainPath)
        {
            fs::path temp = terrainPath;
            temp += ".tmp";
            return temp;
        }

        // A terrain resolved out of a .vfpak is a byte range inside a bigger file. Patching or
        // replacing it would corrupt the archive, and no journal can meaningfully belong to it.
        bool isPackedTerrain(std::string_view path)
        {
            const auto location = locateTerrainFile(std::string(path));
            return location.has_value() && location->baseOffset != 0;
        }

    }

    TerrainRecoveryResult TerrainSerializer::recoverPending(std::string_view path)
    {
        std::unique_lock lock(terrainFileMutex());
        return recoverPendingLocked(path);
    }

    TerrainRecoveryResult TerrainSerializer::recoverPendingLocked(std::string_view path)
    {
        if (terrainArchiveMode() || isPackedTerrain(path))
            return TerrainRecoveryResult::NotNeeded;

        const fs::path live(path);
        const fs::path journalPath = detail::terrainJournalPath(live);
        const fs::path tmpPath = terrainTempPath(live);
        std::error_code ec;

        // VK-1646. A sidecar temporary only ever exists between "both files written" and "both
        // files committed". Whichever side of that window the crash fell on, the temp is now
        // describing a generation no live file is guaranteed to be, so it is swept unconditionally
        // — never promoted. The committed pair is the truth, and if they disagree the load path's
        // stale case already covers it.
        fs::remove(terrainTempPath(terrainLayerSidecarPath(live)), ec);
        ec.clear();

        detail::TerrainJournalRecord record;
        const auto state = detail::readTerrainJournal(journalPath, record);

        const auto discard = [&]() -> TerrainRecoveryResult
        {
            fs::remove(journalPath, ec);
            if (ec) return TerrainRecoveryResult::Failed;
            fs::remove(tmpPath, ec);
            if (ec) return TerrainRecoveryResult::Failed;
            return TerrainRecoveryResult::Discarded;
        };

        if (state == detail::TerrainJournalState::Absent)
        {
            // A surviving .tmp means a full save or a compaction was interrupted before its
            // replacement. Because the replacement is a single directory-entry commit, the
            // destination is necessarily still the previous complete file — and nothing proves the
            // temp is whole, so the conservative answer is to drop it.
            if (!fs::exists(tmpPath, ec) || ec)
                return ec ? TerrainRecoveryResult::Failed : TerrainRecoveryResult::NotNeeded;
            fs::remove(tmpPath, ec);
            return ec ? TerrainRecoveryResult::Failed : TerrainRecoveryResult::Discarded;
        }

        if (state == detail::TerrainJournalState::Invalid)
        {
            vfLogWarning("TerrainSerializer: Discarding an incomplete save journal for {}", path);
            return discard();
        }

        const uint64_t size = fs::file_size(live, ec);
        if (ec)
            return TerrainRecoveryResult::Failed;

        if (size != record.targetSize)
        {
            // The journal describes a file that no longer exists in that shape — the append never
            // finished, or a full save/compaction replaced it. Replaying stale offsets onto a
            // different generation is exactly the corruption this whole mechanism exists to avoid.
            vfLogWarning("TerrainSerializer: Save journal for {} targets size {} but the file is {}; "
                         "discarding", path, record.targetSize, size);
            return discard();
        }

        if (record.indexTableOffset + record.indexBytes.size() > size ||
            record.headerBytes.size() > size)
        {
            vfLogError("TerrainSerializer: Save journal for {} does not fit the file; discarding", path);
            return discard();
        }

        // Deliberately no truncation of any partial tail. Appended-but-unreferenced bytes are inert
        // — the index never points at them and the reader's bounds checks only compare against the
        // file size, which a longer file only relaxes. Truncating would be the single destructive
        // step in recovery, and a stale size would take live data with it. The bytes are counted as
        // obsolete instead, and compaction reclaims them.
        {
            std::fstream file(live, std::ios::binary | std::ios::in | std::ios::out);
            if (!file.is_open())
            {
                vfLogError("TerrainSerializer: Cannot open {} to replay its save journal", path);
                return TerrainRecoveryResult::Failed;
            }

            file.seekp(0, std::ios::beg);
            file.write(reinterpret_cast<const char*>(record.headerBytes.data()),
                       static_cast<std::streamsize>(record.headerBytes.size()));
            file.seekp(static_cast<std::streamoff>(record.indexTableOffset));
            file.write(reinterpret_cast<const char*>(record.indexBytes.data()),
                       static_cast<std::streamsize>(record.indexBytes.size()));
            file.flush();
            if (!file.good())
            {
                vfLogError("TerrainSerializer: Failed to replay the save journal for {}", path);
                return TerrainRecoveryResult::Failed;
            }
        }

        // Durable before the journal is unlinked, so a crash here still leaves the journal to
        // replay. Replay is a plain overwrite with fixed content, so repeating it is harmless.
        if (!resource::flushFileToDisk(live))
            return TerrainRecoveryResult::Failed;

        fs::remove(journalPath, ec);
        if (ec)
        {
            vfLogError("TerrainSerializer: Replayed the save journal for {} but could not remove it", path);
            return TerrainRecoveryResult::Failed;
        }
        fs::remove(tmpPath, ec);

        vfLogInfo("TerrainSerializer: Replayed an interrupted save for {}", path);
        return TerrainRecoveryResult::Redone;
    }

    bool TerrainSerializer::compact(std::string_view path)
    {
        std::unique_lock lock(terrainFileMutex());
        return compactLocked(path);
    }

    bool TerrainSerializer::compactIfNeeded(std::string_view path, bool* outCompacted)
    {
        if (outCompacted)
            *outCompacted = false;

        if (terrainArchiveMode() || isPackedTerrain(path))
            return true;

        std::unique_lock lock(terrainFileMutex());

        TerrainFileHeader header;
        std::vector<TileIndexEntry> index;
        uint64_t indexTableOffset = 0;
        if (!readHeaderLocked(path, header, index, &indexTableOffset))
            return false;

        std::error_code ec;
        const fs::path live(path);
        const uint64_t fileSize = fs::file_size(live, ec);
        if (ec)
            return false;

        const auto usage = terrainFileOccupancy(header, index, indexTableOffset, fileSize);
        if (!usage.consistent)
        {
            vfLogWarning("TerrainSerializer: {} references more bytes than it contains; "
                         "skipping compaction", path);
            return false;
        }

        if (!shouldCompactTerrainFile(usage))
            return true;

        // Compaction builds a full copy beside the original, so it needs room for both.
        const auto parent = live.parent_path();
        const auto space = fs::space(parent.empty() ? fs::path(".") : parent, ec);
        if (!ec && space.available < usage.overheadBytes + usage.liveBytes)
        {
            vfLogWarning("TerrainSerializer: Not enough free space to compact {} "
                         "({} bytes needed); leaving it as it is", path,
                         usage.overheadBytes + usage.liveBytes);
            return false;
        }

        if (!compactLocked(path))
            return false;

        if (outCompacted)
            *outCompacted = true;
        vfLogInfo("TerrainSerializer: Compacted {} — reclaimed {} obsolete bytes",
                  path, usage.obsoleteBytes);
        return true;
    }

    bool TerrainSerializer::compactLocked(std::string_view path)
    {
        if (terrainArchiveMode())
        {
            vfLogError("TerrainSerializer: compaction is disabled while reading from an archive");
            return false;
        }
        if (isPackedTerrain(path))
        {
            vfLogError("TerrainSerializer: cannot compact {}, it lives inside an archive", path);
            return false;
        }

        if (recoverPendingLocked(path) == TerrainRecoveryResult::Failed)
            return false;

        TerrainFileHeader header;
        std::vector<TileIndexEntry> index;
        uint64_t indexTableOffset = 0;
        if (!readHeaderLocked(path, header, index, &indexTableOffset))
            return false;

        const fs::path live(path);
        const fs::path tmpPath = terrainTempPath(live);
        std::error_code ec;

        const uint64_t fileSize = fs::file_size(live, ec);
        if (ec)
            return false;

        // readHeaderLocked() already validated these, but this loop copies bytes without decoding
        // them, so it re-asserts rather than inherits: there is no parse step left to fail safely.
        if (!validateRecordExtents(header, index, indexTableOffset, fileSize))
            return false;

        try
        {
            // Copy in ascending source order so the read is sequential over what can be a
            // multi-gigabyte file; the index itself stays in its original coord-sorted order,
            // which buildSortedIndex() and the readers both depend on.
            std::vector<size_t> order(index.size());
            std::iota(order.begin(), order.end(), size_t{0});
            std::sort(order.begin(), order.end(), [&index](size_t a, size_t b)
            {
                return index[a].heightDataOffset < index[b].heightDataOffset;
            });

            std::ifstream src(live, std::ios::binary);
            if (!src.is_open())
            {
                vfLogError("TerrainSerializer: Cannot open {} to compact it", path);
                return false;
            }

            const auto abandon = [&](const char* reason)
            {
                vfLogError("TerrainSerializer: Compaction of {} failed: {}", path, reason);
                std::error_code removeEc;
                fs::remove(tmpPath, removeEc);
                return false;
            };

            {
                std::ofstream dst(tmpPath, std::ios::binary | std::ios::trunc);
                if (!dst.is_open())
                    return abandon("cannot create the temporary file");

                // The header is re-emitted verbatim, so every feature flag survives compaction
                // exactly — including bits this build does not know about.
                if (!writeHeader(dst, header))
                    return abandon("cannot write the header");

                std::vector<char> placeholder(
                    static_cast<size_t>(header.tileCount) * TILE_INDEX_ENTRY_SIZE, 0);
                dst.write(placeholder.data(), static_cast<std::streamsize>(placeholder.size()));
                if (!dst.good())
                    return abandon("cannot reserve the index table");

                std::vector<char> buffer(COMPACT_COPY_CHUNK_BYTES);
                for (size_t i : order)
                {
                    TileIndexEntry& entry = index[i];

                    const auto position = dst.tellp();
                    if (position == std::streampos(-1))
                        return abandon("lost the output position");
                    const uint64_t newStart = static_cast<uint64_t>(position);
                    const int64_t delta = static_cast<int64_t>(newStart) -
                                          static_cast<int64_t>(entry.heightDataOffset);

                    const auto fault = detail::terrainSaveFault(TerrainSaveStage::CompactionCopy);
                    uint64_t remaining = fault.abort
                        ? std::min<uint64_t>(fault.partialBytes, entry.payloadSize)
                        : entry.payloadSize;

                    src.seekg(static_cast<std::streamoff>(entry.heightDataOffset));
                    while (remaining > 0)
                    {
                        const size_t chunk =
                            static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
                        if (!src.read(buffer.data(), static_cast<std::streamsize>(chunk)))
                            return abandon("truncated read of a tile record");
                        dst.write(buffer.data(), static_cast<std::streamsize>(chunk));
                        if (!dst.good())
                            return abandon("failed write of a tile record");
                        remaining -= chunk;
                    }

                    if (fault.abort)
                        return abandon("interrupted while copying a tile record");

                    // The record moved as opaque bytes, so every sub-block inside it moved by the
                    // same amount. That is only true because writeTileData() lays a record out
                    // contiguously and stores no absolute offsets inside it.
                    entry.heightDataOffset = newStart;
                    for (uint64_t* offset : {&entry.weightDataOffset, &entry.meshletDataOffset,
                                             &entry.holeMaskDataOffset, &entry.caveSdfDataOffset})
                    {
                        if (*offset != 0)
                            *offset = static_cast<uint64_t>(static_cast<int64_t>(*offset) + delta);
                    }
                }

                dst.seekp(static_cast<std::streamoff>(indexTableOffset));
                if (!writeIndexTable(dst, index))
                    return abandon("cannot write the index table");

                dst.flush();
                if (!dst.good())
                    return abandon("failed to flush the compacted file");
            }
            src.close();

            if (detail::terrainSaveFault(TerrainSaveStage::BeforeReplace).abort)
                return false;

            // Same ordering rule as the full save: any pending journal describes the generation we
            // are about to replace, so it must go before the swap, never after.
            fs::remove(detail::terrainJournalPath(live), ec);
            if (ec)
            {
                vfLogError("TerrainSerializer: Could not clear the save journal before compacting {}", path);
                return false;
            }

            // VK-1646 needs nothing here. Compaction relocates every tile record but re-emits the
            // header verbatim, so editLayerGenerationId comes through untouched and the sidecar
            // stays bound. That is precisely why the binding is an opaque id rather than a hash of
            // the file's bytes: a hash would call a perfectly good sidecar stale after a pure
            // byte-shuffle, and cost the artist their layer stack for reclaiming disk space.
            return resource::replaceFileAtomically(tmpPath, live);
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Compaction of {} threw: {}", path, e.what());
            std::error_code removeEc;
            fs::remove(tmpPath, removeEc);
            return false;
        }
    }
}
