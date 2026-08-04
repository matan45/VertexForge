#include "TerrainSerializer.hpp"
#include "TerrainCompression.hpp"
#include "TerrainFileAccess.hpp"
#include "TerrainSaveFaultInjection.hpp"
#include "TerrainSaveJournal.hpp"
#include "../print/Log.hpp"
#include "TerrainGrid.hpp"
#include "../resource/AtomicFileReplace.hpp"
#include "../resource/EndianUtils.hpp"
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <mutex>
#include <shared_mutex>
#include <sstream>

namespace terrain
{
    namespace fs = std::filesystem;

    TerrainFormatFlags TerrainSerializer::computeFlags(const TerrainGrid& grid,
                                                       const TerrainPhysicsConfig& physicsConfig,
                                                       const TerrainStreamingConfig& streamingConfig)
    {
        TerrainFormatFlags flags = TerrainFormatFlags::HAS_COMPRESSED_DATA;

        auto allTiles = grid.getAllTiles();

        for (const auto* tile : allTiles)
        {
            if (tile->weightMap.isInitialized())
            {
                flags = flags | TerrainFormatFlags::HAS_WEIGHT_MAPS;
                break;
            }
        }
        for (const auto* tile : allTiles)
        {
            if (!tile->lodLevels.empty() && tile->lodLevels[0].hasMeshlets())
            {
                flags = flags | TerrainFormatFlags::HAS_MESHLET_CACHE;
                break;
            }
        }
        if (physicsConfig.hasCollider)
            flags = flags | TerrainFormatFlags::HAS_PHYSICS_DATA;
        if (streamingConfig.enabled)
            flags = flags | TerrainFormatFlags::HAS_STREAMING_CONFIG;

        for (const auto* tile : allTiles)
        {
            if (tile->hasHoleMask())
            {
                bool hasAnyHole = false;
                for (uint8_t h : tile->holeMask)
                {
                    if (h) { hasAnyHole = true; break; }
                }
                if (hasAnyHole)
                {
                    flags = flags | TerrainFormatFlags::HAS_HOLE_MASK;
                    break;
                }
            }
        }

        for (const auto* tile : allTiles)
        {
            if (tile->hasCaveData() && tile->caveData->hasCaveGeometry())
            {
                flags = flags | TerrainFormatFlags::HAS_CAVE_DATA;
                break;
            }
        }

        return flags;
    }

    TerrainFormatFlags TerrainSerializer::mergeIncrementalFlags(TerrainFormatFlags onDisk,
                                                                 TerrainFormatFlags computed)
    {
        // computeFlags() only sees grid.getAllTiles(), i.e. the RESIDENT tiles. On the incremental
        // path that is a subset of the file, so taking its answer verbatim can clear a flag that
        // only a streamed-out tile justifies. That is not cosmetic: writeTileData() gates each
        // optional sub-block on these flags, so a cleared HAS_MESHLET_CACHE means the tiles being
        // rewritten lose their meshlet cache outright, and TerrainFileCache::hasMeshletCache()
        // then stops every other tile's cache from ever being read again.
        //
        // Unioning also preserves bits this build knows nothing about — bit 6 is reserved for
        // HAS_EDIT_LAYER_SIDECAR, and a plain assignment silently destroys it.
        constexpr uint32_t configBits =
            static_cast<uint32_t>(TerrainFormatFlags::HAS_PHYSICS_DATA) |
            static_cast<uint32_t>(TerrainFormatFlags::HAS_STREAMING_CONFIG);

        // The two config bits are authoritatively derived from the live configuration rather than
        // from tile content, so they are the only ones allowed to clear. Toggling either resizes
        // the header anyway, which validateIncrementalHeaderLayout() refuses outright.
        const uint32_t unioned = static_cast<uint32_t>(onDisk) | static_cast<uint32_t>(computed);
        const uint32_t configured = static_cast<uint32_t>(computed) & configBits;
        return static_cast<TerrainFormatFlags>((unioned & ~configBits) | configured);
    }

    bool TerrainSerializer::validateIncrementalHeaderLayout(const TerrainFileHeader& updatedHeader,
                                                             uint64_t indexTableOffset)
    {
        // Incremental save rewrites the header in place and then seeks to the *cached* index
        // table offset. That is only sound while the rewritten header occupies exactly the same
        // bytes -- the material path is variable length and the physics/streaming blocks are
        // optional, so any of them can move the index table.
        // NOTE: TerrainService::prepareSaveIncremental() has a matching guard on the main thread
        // built from the same serializedHeaderSize(). Both must agree -- if updating one, update
        // the other. The main-thread guard may be more eager, never less: the background fallback
        // to a full save runs without prepareSave() and would drop streamed-out tiles.
        if (serializedHeaderSize(updatedHeader) != indexTableOffset)
        {
            vfLogWarning("TerrainSerializer: Header size changed, falling back to full save");
            return false;
        }
        return true;
    }

    std::vector<TileIndexEntry> TerrainSerializer::buildSortedIndex(
        const std::unordered_map<TileCoord, TileIndexEntry, TileCoordHash>& indexMap)
    {
        std::vector<TileIndexEntry> entries;
        entries.reserve(indexMap.size());
        for (const auto& [coord, entry] : indexMap)
            entries.push_back(entry);

        std::sort(entries.begin(), entries.end(),
            [](const TileIndexEntry& a, const TileIndexEntry& b) {
                if (a.coordX != b.coordX) return a.coordX < b.coordX;
                return a.coordZ < b.coordZ;
            });
        return entries;
    }

    bool TerrainSerializer::serializeHeaderToBytes(const TerrainFileHeader& header,
                                                    std::vector<uint8_t>& out)
    {
        // Runs the real writer into memory rather than re-listing the fields, so the journal image
        // cannot drift from what the file writer emits.
        std::ostringstream stream(std::ios::binary);
        if (!writeHeader(stream, header))
            return false;
        const std::string bytes = stream.str();
        out.assign(bytes.begin(), bytes.end());
        return true;
    }

    bool TerrainSerializer::serializeIndexToBytes(const std::vector<TileIndexEntry>& index,
                                                   std::vector<uint8_t>& out)
    {
        std::ostringstream stream(std::ios::binary);
        if (!writeIndexTable(stream, index))
            return false;
        const std::string bytes = stream.str();
        out.assign(bytes.begin(), bytes.end());
        return true;
    }

    bool TerrainSerializer::commitIncrementalBuffers(std::fstream& file,
                                                      uint64_t indexTableOffset,
                                                      const std::vector<uint8_t>& headerBytes,
                                                      const std::vector<uint8_t>& indexBytes)
    {
        const auto headerFault = detail::terrainSaveFault(TerrainSaveStage::HeaderCommit);
        const size_t headerCount = headerFault.abort
            ? static_cast<size_t>(std::min<uint64_t>(headerFault.partialBytes, headerBytes.size()))
            : headerBytes.size();

        file.seekp(0, std::ios::beg);
        file.write(reinterpret_cast<const char*>(headerBytes.data()),
                   static_cast<std::streamsize>(headerCount));
        file.flush();
        if (headerFault.abort)
            return false;
        if (!file.good())
        {
            vfLogError("TerrainSerializer: Failed to rewrite header");
            return false;
        }

        const auto indexFault = detail::terrainSaveFault(TerrainSaveStage::IndexCommit);
        const size_t indexCount = indexFault.abort
            ? static_cast<size_t>(std::min<uint64_t>(indexFault.partialBytes, indexBytes.size()))
            : indexBytes.size();

        file.seekp(static_cast<std::streamoff>(indexTableOffset));
        file.write(reinterpret_cast<const char*>(indexBytes.data()),
                   static_cast<std::streamsize>(indexCount));
        file.flush();
        if (indexFault.abort)
            return false;
        if (!file.good())
        {
            vfLogError("TerrainSerializer: Failed to rewrite index table");
            return false;
        }
        return true;
    }

    bool TerrainSerializer::writeIncrementalTiles(std::ostream& file,
                                                   const TerrainGrid& grid,
                                                   const std::unordered_set<TileCoord, TileCoordHash>& dirtyCoords,
                                                   TerrainFormatFlags flags,
                                                   std::vector<TileIndexEntry>& indexEntries)
    {
        for (const auto& coord : dirtyCoords)
        {
            const TerrainTile* tile = grid.getTile(coord);
            if (!tile)
            {
                vfLogWarning("TerrainSerializer: Dirty tile ({}, {}) not in grid, skipping",
                             coord.x, coord.z);
                continue;
            }

            auto slot = std::find_if(indexEntries.begin(), indexEntries.end(),
                [&coord](const TileIndexEntry& entry)
                {
                    return entry.coordX == coord.x && entry.coordZ == coord.z;
                });
            if (slot == indexEntries.end())
            {
                // Appending a record no index entry can point at would write pure garbage AND lose
                // the tile's edits, since refreshIndex() clears the dirty set afterwards either
                // way. Refuse instead, so the caller full-saves — that path does have a slot for a
                // new tile. TerrainService::prepareSaveIncremental() carries the matching guard.
                vfLogWarning("TerrainSerializer: Dirty tile ({}, {}) has no index entry, "
                             "falling back to full save", coord.x, coord.z);
                return false;
            }

            const auto fault = detail::terrainSaveFault(TerrainSaveStage::PayloadAppend);
            if (fault.abort)
            {
                // Serialize into memory so the record can be cut at an arbitrary byte, which is
                // what a crash mid-append actually leaves behind. The scratch entry's offsets are
                // relative to the buffer and deliberately discarded — we are aborting, and nothing
                // will reference these bytes.
                std::ostringstream record(std::ios::binary);
                TileIndexEntry scratch{};
                writeTileData(record, *tile, flags, scratch);
                const std::string bytes = record.str();
                const size_t count =
                    static_cast<size_t>(std::min<uint64_t>(fault.partialBytes, bytes.size()));
                file.write(bytes.data(), static_cast<std::streamsize>(count));
                file.flush();
                return false;
            }

            TileIndexEntry newEntry{};
            if (!writeTileData(file, *tile, flags, newEntry))
            {
                vfLogError("TerrainSerializer: Failed to write dirty tile ({}, {})",
                           coord.x, coord.z);
                return false;
            }

            *slot = newEntry;
        }
        return true;
    }

    TerrainIncrementalSaveResult TerrainSerializer::saveIncremental(
        const TerrainIncrementalSaveParams& params)
    {
        if (terrainArchiveMode())
        {
            vfLogError("TerrainSerializer: incremental saves are disabled while reading from an archive");
            return TerrainIncrementalSaveResult::Failed;
        }

        if (!params.dirtyCoords || params.dirtyCoords->empty())
            return TerrainIncrementalSaveResult::Success;

        if (!params.grid || !params.currentIndexMap)
            return TerrainIncrementalSaveResult::Failed;

        std::unique_lock lock(terrainFileMutex());

        fs::path filePath(params.path);
        if (!fs::exists(filePath))
        {
            vfLogError("TerrainSerializer: File not found for incremental save: {}", params.path);
            return TerrainIncrementalSaveResult::Failed;
        }

        // Settle any interrupted earlier commit before reading anything about this file.
        const auto recovery = recoverPendingLocked(params.path);
        if (recovery == TerrainRecoveryResult::Failed)
            return TerrainIncrementalSaveResult::Failed;
        if (recovery != TerrainRecoveryResult::NotNeeded)
        {
            // Recovery just moved the on-disk header and index out from under the caller's cached
            // snapshot. Reporting Failed rather than NeedsFullSave is deliberate: the background
            // full-save fallback runs without prepareSave(), so it would write only the resident
            // tiles. A failed save the user retries costs a click; that fallback costs terrain.
            vfLogWarning("TerrainSerializer: Recovered an interrupted save for {}; "
                         "the cached index is stale, retry the save", params.path);
            return TerrainIncrementalSaveResult::Failed;
        }

        try
        {
            // Build the header we intend to write back and validate its layout *before* opening
            // the file, so a refusal leaves the terrain bytes untouched.
            TerrainFileHeader updatedHeader = params.currentHeader;
            updatedHeader.flags = mergeIncrementalFlags(
                params.currentHeader.flags,
                computeFlags(*params.grid, params.physicsConfig, params.streamingConfig));
            updatedHeader.physicsConfig = params.physicsConfig;
            updatedHeader.streamingConfig = params.streamingConfig;
            updatedHeader.materialPath = params.materialPath;

            // The caller's cached offset must still describe the header on disk, otherwise the
            // index table is not where we think it is and nothing below can be trusted.
            if (serializedHeaderSize(params.currentHeader) != params.indexTableOffset)
            {
                vfLogWarning("TerrainSerializer: Cached index table offset {} does not match the "
                             "on-disk header size {}, falling back to full save",
                             params.indexTableOffset, serializedHeaderSize(params.currentHeader));
                return TerrainIncrementalSaveResult::NeedsFullSave;
            }

            if (!validateIncrementalHeaderLayout(updatedHeader, params.indexTableOffset))
                return TerrainIncrementalSaveResult::NeedsFullSave;

            for (const auto& coord : *params.dirtyCoords)
            {
                if (params.currentIndexMap->find(coord) == params.currentIndexMap->end())
                {
                    vfLogWarning("TerrainSerializer: Dirty tile ({}, {}) is absent from the cached "
                                 "index, falling back to full save", coord.x, coord.z);
                    return TerrainIncrementalSaveResult::NeedsFullSave;
                }
            }

            std::fstream file(filePath, std::ios::binary | std::ios::in | std::ios::out);
            if (!file.is_open())
            {
                vfLogError("TerrainSerializer: Failed to open file for incremental save: {}", params.path);
                return TerrainIncrementalSaveResult::Failed;
            }

            std::vector<TileIndexEntry> indexEntries = buildSortedIndex(*params.currentIndexMap);

            file.seekp(0, std::ios::end);
            const auto preAppendPos = file.tellp();
            if (preAppendPos == std::streampos(-1))
                return TerrainIncrementalSaveResult::Failed;
            const uint64_t preAppendSize = static_cast<uint64_t>(preAppendPos);

            if (!writeIncrementalTiles(file, *params.grid, *params.dirtyCoords, updatedHeader.flags,
                                       indexEntries))
                return TerrainIncrementalSaveResult::Failed;

            file.flush();
            if (!file.good())
                return TerrainIncrementalSaveResult::Failed;

            // The journal's index entries point at the bytes just appended, so those bytes must be
            // durable before the journal that references them is. Otherwise recovery could publish
            // an index into a region the disk never received.
            if (!resource::flushFileToDisk(filePath))
                return TerrainIncrementalSaveResult::Failed;

            if (detail::terrainSaveFault(TerrainSaveStage::AfterPayloadFlush).abort)
                return TerrainIncrementalSaveResult::Failed;

            file.seekp(0, std::ios::end);
            const auto targetPos = file.tellp();
            if (targetPos == std::streampos(-1))
                return TerrainIncrementalSaveResult::Failed;

            std::vector<uint8_t> headerBytes;
            std::vector<uint8_t> indexBytes;
            if (!serializeHeaderToBytes(updatedHeader, headerBytes) ||
                !serializeIndexToBytes(indexEntries, indexBytes))
                return TerrainIncrementalSaveResult::Failed;

            // Both are structural invariants of the incremental path rather than assumptions: the
            // header image is exactly as long as the index table offset (that is what
            // serializedHeaderSize means), and tileCount cannot change without a full save.
            if (headerBytes.size() != params.indexTableOffset ||
                indexBytes.size() !=
                    static_cast<size_t>(updatedHeader.tileCount) * TILE_INDEX_ENTRY_SIZE)
            {
                vfLogError("TerrainSerializer: Incremental commit images do not match the file "
                           "layout for {}", params.path);
                return TerrainIncrementalSaveResult::Failed;
            }

            detail::TerrainJournalRecord journal;
            journal.preAppendSize = preAppendSize;
            journal.targetSize = static_cast<uint64_t>(targetPos);
            journal.indexTableOffset = params.indexTableOffset;
            journal.headerBytes = headerBytes;
            journal.indexBytes = indexBytes;

            const fs::path journalPath = detail::terrainJournalPath(filePath);
            if (!detail::writeTerrainJournal(journalPath, journal))
                return TerrainIncrementalSaveResult::Failed;

            if (detail::terrainSaveFault(TerrainSaveStage::AfterJournalCommit).abort)
                return TerrainIncrementalSaveResult::Failed;

            if (!commitIncrementalBuffers(file, params.indexTableOffset, headerBytes, indexBytes))
                return TerrainIncrementalSaveResult::Failed;

            file.close();
            if (!resource::flushFileToDisk(filePath))
                return TerrainIncrementalSaveResult::Failed;

            if (detail::terrainSaveFault(TerrainSaveStage::BeforeJournalDelete).abort)
                return TerrainIncrementalSaveResult::Failed;

            std::error_code ec;
            fs::remove(journalPath, ec);
            if (ec)
            {
                // Harmless: replaying it writes the bytes that are already there. Recovery will
                // clear it on the next open.
                vfLogWarning("TerrainSerializer: Could not remove {} after committing",
                             journalPath.string());
            }

            vfLogInfo("TerrainSerializer: Incremental save: updated {} dirty tiles in {}",
                      params.dirtyCoords->size(), params.path);
            return TerrainIncrementalSaveResult::Success;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Incremental save failed for {}: {}", params.path, e.what());
            return TerrainIncrementalSaveResult::Failed;
        }
    }

    TerrainFileHeader TerrainSerializer::buildSaveHeader(const TerrainSaveParams& params,
                                                         TerrainFormatFlags flags,
                                                         uint32_t tileCount)
    {
        TerrainFileHeader header;
        header.flags = flags;
        header.tileCount = tileCount;
        header.resolution = static_cast<uint8_t>(params.config.resolution);
        header.worldTileSize = params.config.worldTileSize;
        header.maxHeight = params.config.maxHeight;
        header.minHeight = params.config.minHeight;
        header.skirtDepth = params.config.skirtDepth;
        header.gridMinX = params.gridMinX;
        header.gridMinZ = params.gridMinZ;
        header.gridMaxX = params.gridMaxX;
        header.gridMaxZ = params.gridMaxZ;
        header.materialPath = params.materialPath;
        header.physicsConfig = params.physicsConfig;
        header.streamingConfig = params.streamingConfig;
        return header;
    }

    bool TerrainSerializer::writeAllTileData(std::ostream& file,
                                              const std::vector<const TerrainTile*>& tiles,
                                              TerrainFormatFlags flags,
                                              std::vector<TileIndexEntry>& indexEntries)
    {
        for (uint32_t i = 0; i < static_cast<uint32_t>(tiles.size()); ++i)
        {
            if (!writeTileData(file, *tiles[i], flags, indexEntries[i]))
            {
                vfLogError("TerrainSerializer: Failed to write tile ({}, {})",
                           tiles[i]->coord.x, tiles[i]->coord.z);
                return false;
            }
        }
        return true;
    }

    bool TerrainSerializer::writeFullSaveToStream(std::ostream& file,
                                                    const TerrainFileHeader& header,
                                                    const std::vector<const TerrainTile*>& tiles,
                                                    TerrainFormatFlags flags)
    {
        if (!writeHeader(file, header))
        {
            vfLogError("TerrainSerializer: Failed to write header");
            return false;
        }

        auto indexTablePos = file.tellp();
        if (indexTablePos == std::streampos(-1))
        {
            vfLogError("TerrainSerializer: Failed to get index table position");
            return false;
        }
        std::vector<char> placeholder(header.tileCount * TILE_INDEX_ENTRY_SIZE, 0);
        file.write(placeholder.data(), static_cast<std::streamsize>(placeholder.size()));

        if (!file.good())
        {
            vfLogError("TerrainSerializer: Failed to write index placeholder");
            return false;
        }

        std::vector<TileIndexEntry> indexEntries(header.tileCount);
        if (!writeAllTileData(file, tiles, flags, indexEntries))
            return false;

        file.seekp(indexTablePos);
        if (!writeIndexTable(file, indexEntries))
        {
            vfLogError("TerrainSerializer: Failed to write index table");
            return false;
        }

        file.flush();
        return file.good();
    }

    bool TerrainSerializer::save(const TerrainSaveParams& params)
    {
        if (terrainArchiveMode())
        {
            vfLogError("TerrainSerializer: saves are disabled while reading from an archive");
            return false;
        }

        if (!params.grid)
            return false;

        auto allTiles = params.grid->getAllTiles();
        if (allTiles.empty())
        {
            vfLogWarning("TerrainSerializer: No tiles to save");
            return true;
        }

        std::sort(allTiles.begin(), allTiles.end(),
                  [](const TerrainTile* a, const TerrainTile* b)
                  {
                      if (a->coord.x != b->coord.x) return a->coord.x < b->coord.x;
                      return a->coord.z < b->coord.z;
                  });

        TerrainFormatFlags flags = computeFlags(*params.grid, params.physicsConfig, params.streamingConfig);
        TerrainFileHeader header = buildSaveHeader(params, flags, static_cast<uint32_t>(allTiles.size()));

        std::unique_lock lock(terrainFileMutex());

        try
        {
            fs::path filePath(params.path);
            fs::create_directories(filePath.parent_path());
            fs::path tmpPath = filePath;
            tmpPath += ".tmp";

            std::error_code ec;
            {
                std::ofstream file(tmpPath, std::ios::binary);
                if (!file.is_open())
                {
                    vfLogError("TerrainSerializer: Failed to create file: {}", params.path);
                    return false;
                }

                if (!writeFullSaveToStream(file, header, allTiles, flags))
                {
                    vfLogError("TerrainSerializer: Failed to flush file: {}", params.path);
                    file.close();
                    fs::remove(tmpPath, ec);
                    return false;
                }
            }

            if (detail::terrainSaveFault(TerrainSaveStage::BeforeReplace).abort)
                return false;

            // A full rewrite supersedes any pending incremental commit, and that commit's byte
            // offsets describe the file we are about to discard. Drop the journal now, while the
            // destination is still the old file — doing it after the swap would leave a window in
            // which a crash strands a journal aimed at the wrong generation, and replaying that
            // would corrupt a perfectly good file. The cost of dropping it here is at most the
            // last incremental save, whose edits this full save is writing anyway.
            const fs::path journalPath = detail::terrainJournalPath(filePath);
            fs::remove(journalPath, ec);
            if (ec)
            {
                vfLogError("TerrainSerializer: Could not clear {} before replacing the terrain",
                           journalPath.string());
                return false;
            }

            if (!resource::replaceFileAtomically(tmpPath, filePath))
            {
                vfLogError("TerrainSerializer: Failed to replace {}", params.path);
                return false;
            }

            vfLogInfo("TerrainSerializer: Saved {} tiles to {}", header.tileCount, params.path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("TerrainSerializer: Failed to save {}: {}", params.path, e.what());
            return false;
        }
    }
}
