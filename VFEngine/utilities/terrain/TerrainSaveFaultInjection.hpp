#pragma once

#include "TerrainExport.hpp"
#include <cstdint>
#include <functional>
#include <limits>

namespace terrain
{
    // The points at which a save can be interrupted by a crash. Tests arm an injector to stop the
    // writer at one of them and then assert that what is left on disk is recoverable.
    enum class TerrainSaveStage : uint32_t
    {
        PayloadAppend,        // appending a dirty tile's record at EOF
        AfterPayloadFlush,    // appended records durable, journal not yet written
        JournalWrite,         // writing the .vftrj body
        AfterJournalCommit,   // journal durable, live header/index not yet patched
        HeaderCommit,         // writing the header image at offset 0
        IndexCommit,          // writing the index image at indexTableOffset
        BeforeJournalDelete,  // patch durable, journal still present
        CompactionCopy,       // copying a tile record into the compaction temp
        BeforeReplace,        // temp file complete and flushed, replacement not started
    };

    struct TerrainSaveFault
    {
        bool abort = false;
        // How many bytes of this stage's buffer to write before aborting.
        //
        // A plain "abort here" boolean is not enough to reproduce the failure this ticket is about.
        // std::fstream is buffered and its filebuf flushes on destruction regardless of stream
        // state, so aborting between whole writes can only ever produce whole-write granularity —
        // never the torn header or half-written 56-byte index entry that a real crash leaves and
        // that recovery has to repair. Stopping part-way through a precomputed byte buffer is what
        // makes those states reproducible, and deterministic.
        uint64_t partialBytes = (std::numeric_limits<uint64_t>::max)();
    };

    using TerrainSaveFaultInjector = std::function<TerrainSaveFault(TerrainSaveStage)>;

    // Test-only seam, shaped after setTerrainFileAccess(): a process-wide callback guarded by a
    // shared_mutex, with the callback copied out before it is invoked so the lock is never held
    // across user code. Saves run on a JobSystem worker, so a bare global std::function would be a
    // data race.
    VF_TERRAIN_API void setTerrainSaveFaultInjector(TerrainSaveFaultInjector injector);
    VF_TERRAIN_API void resetTerrainSaveFaultInjector();

    namespace detail
    {
        // Guarded by an atomic armed flag, so the cost on the normal path is one relaxed load.
        VF_TERRAIN_API TerrainSaveFault terrainSaveFault(TerrainSaveStage stage);
    }
}
