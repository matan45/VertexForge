#include "TerrainSaveJournal.hpp"

#include "TerrainSaveFaultInjection.hpp"
#include "TerrainSerializer.hpp"
#include "../archive/VFPakFormat.hpp"
#include "../print/Log.hpp"
#include "../resource/AtomicFileReplace.hpp"
#include "../resource/EndianUtils.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <sstream>

namespace terrain::detail
{
    namespace
    {
        namespace fs = std::filesystem;
        using namespace resource::endian;

        constexpr std::array<char, 4> JOURNAL_MAGIC = {'V', 'F', 'T', 'J'};
        constexpr uint64_t JOURNAL_COMMIT_MAGIC = 0x5646544A434D5449ull;

        // magic + 3 version words + preAppendSize + targetSize + indexTableOffset
        // + headerBytesLen + indexBytesLen + bodyHash
        constexpr size_t JOURNAL_FIXED_SIZE =
            4 + 3 * sizeof(uint32_t) + 3 * sizeof(uint64_t) + 2 * sizeof(uint32_t) + sizeof(uint64_t);
        static_assert(JOURNAL_FIXED_SIZE == 56, "Journal prefix layout changed");

        constexpr size_t JOURNAL_TRAILER_SIZE = sizeof(uint64_t) + sizeof(uint64_t);

        uint64_t hashOf(const std::vector<uint8_t>& bytes)
        {
            return archive::hashBytes(bytes.data(), bytes.size());
        }
    }

    fs::path terrainJournalPath(const fs::path& terrainPath)
    {
        fs::path journal = terrainPath;
        journal += ".vftrj";
        return journal;
    }

    bool writeTerrainJournal(const fs::path& journalPath, const TerrainJournalRecord& record)
    {
        std::ostringstream body(std::ios::binary);
        body.write(JOURNAL_MAGIC.data(), 4);
        writeLE<uint32_t>(body, TERRAIN_FORMAT_VERSION_MAJOR);
        writeLE<uint32_t>(body, TERRAIN_FORMAT_VERSION_MINOR);
        writeLE<uint32_t>(body, TERRAIN_FORMAT_VERSION_PATCH);
        writeLE<uint64_t>(body, record.preAppendSize);
        writeLE<uint64_t>(body, record.targetSize);
        writeLE<uint64_t>(body, record.indexTableOffset);
        writeLE<uint32_t>(body, static_cast<uint32_t>(record.headerBytes.size()));
        writeLE<uint32_t>(body, static_cast<uint32_t>(record.indexBytes.size()));

        std::vector<uint8_t> payload;
        payload.reserve(record.headerBytes.size() + record.indexBytes.size());
        payload.insert(payload.end(), record.headerBytes.begin(), record.headerBytes.end());
        payload.insert(payload.end(), record.indexBytes.begin(), record.indexBytes.end());
        writeLE<uint64_t>(body, hashOf(payload));

        body.write(reinterpret_cast<const char*>(payload.data()),
                   static_cast<std::streamsize>(payload.size()));
        if (!body.good())
            return false;

        // The trailer's hash covers everything before it, so "committed" is an all-or-nothing fact:
        // a torn write leaves a journal that fails this check and is therefore discarded, which
        // costs the last save's edits but never the file's integrity.
        const std::string prefix = body.str();
        std::vector<uint8_t> bytes(prefix.begin(), prefix.end());
        const uint64_t commitHash = hashOf(bytes);

        std::ostringstream trailer(std::ios::binary);
        writeLE<uint64_t>(trailer, commitHash);
        writeLE<uint64_t>(trailer, JOURNAL_COMMIT_MAGIC);
        const std::string trailerBytes = trailer.str();
        bytes.insert(bytes.end(), trailerBytes.begin(), trailerBytes.end());

        const auto fault = terrainSaveFault(TerrainSaveStage::JournalWrite);
        const size_t toWrite = fault.abort
            ? static_cast<size_t>(std::min<uint64_t>(fault.partialBytes, bytes.size()))
            : bytes.size();

        {
            std::ofstream file(journalPath, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                vfLogError("TerrainSaveJournal: Cannot create {}", journalPath.string());
                return false;
            }
            file.write(reinterpret_cast<const char*>(bytes.data()),
                       static_cast<std::streamsize>(toWrite));
            file.flush();
            if (!file.good())
            {
                vfLogError("TerrainSaveJournal: Failed to write {}", journalPath.string());
                return false;
            }
        }

        if (!resource::flushFileToDisk(journalPath))
            return false;

        return !fault.abort;
    }

    TerrainJournalState readTerrainJournal(const fs::path& journalPath, TerrainJournalRecord& out)
    {
        std::error_code ec;
        if (!fs::exists(journalPath, ec) || ec)
            return TerrainJournalState::Absent;

        std::ifstream file(journalPath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return TerrainJournalState::Invalid;

        const auto end = file.tellg();
        if (end < 0 || static_cast<size_t>(end) < JOURNAL_FIXED_SIZE + JOURNAL_TRAILER_SIZE)
            return TerrainJournalState::Invalid;

        const size_t size = static_cast<size_t>(end);
        std::vector<uint8_t> bytes(size);
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
            return TerrainJournalState::Invalid;

        std::istringstream stream(std::string(bytes.begin(), bytes.end()), std::ios::binary);
        std::array<char, 4> magic{};
        stream.read(magic.data(), 4);
        if (magic != JOURNAL_MAGIC)
            return TerrainJournalState::Invalid;

        const uint32_t major = readLE<uint32_t>(stream);
        const uint32_t minor = readLE<uint32_t>(stream);
        const uint32_t patch = readLE<uint32_t>(stream);
        if (major != TERRAIN_FORMAT_VERSION_MAJOR || minor != TERRAIN_FORMAT_VERSION_MINOR ||
            patch != TERRAIN_FORMAT_VERSION_PATCH)
            return TerrainJournalState::Invalid;

        TerrainJournalRecord record;
        record.preAppendSize = readLE<uint64_t>(stream);
        record.targetSize = readLE<uint64_t>(stream);
        record.indexTableOffset = readLE<uint64_t>(stream);
        const uint32_t headerLen = readLE<uint32_t>(stream);
        const uint32_t indexLen = readLE<uint32_t>(stream);
        const uint64_t bodyHash = readLE<uint64_t>(stream);
        if (!stream.good())
            return TerrainJournalState::Invalid;

        // Exact size is part of the validity predicate: a journal cut short mid-payload has the
        // right prefix and would otherwise be parsed as if it were whole.
        if (size != JOURNAL_FIXED_SIZE + headerLen + indexLen + JOURNAL_TRAILER_SIZE)
            return TerrainJournalState::Invalid;

        // Structural cross-checks against the format itself. A header image is exactly as long as
        // the index table offset by construction — that is what serializedHeaderSize() means — and
        // the index is a whole number of entries.
        if (headerLen != record.indexTableOffset || indexLen % TILE_INDEX_ENTRY_SIZE != 0)
            return TerrainJournalState::Invalid;

        std::istringstream trailerStream(
            std::string(bytes.begin() + static_cast<ptrdiff_t>(size - JOURNAL_TRAILER_SIZE),
                        bytes.end()),
            std::ios::binary);
        const uint64_t commitHash = readLE<uint64_t>(trailerStream);
        const uint64_t commitMagic = readLE<uint64_t>(trailerStream);
        if (commitMagic != JOURNAL_COMMIT_MAGIC)
            return TerrainJournalState::Invalid;

        const std::vector<uint8_t> covered(bytes.begin(),
                                           bytes.end() - static_cast<ptrdiff_t>(JOURNAL_TRAILER_SIZE));
        if (archive::hashBytes(covered.data(), covered.size()) != commitHash)
            return TerrainJournalState::Invalid;

        const auto payloadBegin = bytes.begin() + static_cast<ptrdiff_t>(JOURNAL_FIXED_SIZE);
        record.headerBytes.assign(payloadBegin, payloadBegin + headerLen);
        record.indexBytes.assign(payloadBegin + headerLen, payloadBegin + headerLen + indexLen);

        const std::vector<uint8_t> payload(payloadBegin, payloadBegin + headerLen + indexLen);
        if (hashOf(payload) != bodyHash)
            return TerrainJournalState::Invalid;

        out = std::move(record);
        return TerrainJournalState::Valid;
    }
}
