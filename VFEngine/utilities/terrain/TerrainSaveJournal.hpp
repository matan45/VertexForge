#pragma once

#include "TerrainExport.hpp"
#include <cstdint>
#include <filesystem>
#include <vector>

namespace terrain::detail
{
    // The redo record for one incremental commit.
    //
    // An incremental save appends new tile records past EOF and then overwrites the header and the
    // index table in place. The append is harmless on its own — nothing on disk references those
    // bytes yet — but the overwrite is not: a crash part-way through leaves a header spliced from
    // two generations, or an index entry whose offsets come half from each, and neither is
    // detectable afterwards. So the exact bytes of that overwrite are made durable here first, and
    // replaying them is a plain write of two fixed ranges with fixed content: idempotent, and safe
    // to repeat if recovery is itself interrupted.
    struct TerrainJournalRecord
    {
        uint64_t preAppendSize = 0;    // file size before the append (diagnostics and tests)
        uint64_t targetSize = 0;       // file size after the append — must match, or this is stale
        uint64_t indexTableOffset = 0; // where indexBytes belongs
        std::vector<uint8_t> headerBytes;  // to be written at offset 0
        std::vector<uint8_t> indexBytes;   // to be written at indexTableOffset
    };

    VF_TERRAIN_API std::filesystem::path terrainJournalPath(const std::filesystem::path& terrainPath);

    // Writes, flushes and pushes the journal to disk. Returns false if any of that fails — the
    // caller must then abort before touching the live file's header or index.
    bool writeTerrainJournal(const std::filesystem::path& journalPath,
                             const TerrainJournalRecord& record);

    enum class TerrainJournalState
    {
        Absent,
        Invalid,   // truncated, mistyped, or hash mismatch — discard it
        Valid
    };

    TerrainJournalState readTerrainJournal(const std::filesystem::path& journalPath,
                                           TerrainJournalRecord& out);
}
