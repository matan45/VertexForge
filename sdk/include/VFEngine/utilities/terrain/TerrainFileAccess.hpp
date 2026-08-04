#pragma once

#include "TerrainExport.hpp"
#include <cstdint>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace terrain
{
    struct TerrainFileLocation
    {
        std::string filePath;
        uint64_t baseOffset = 0;
        uint64_t size = 0;
    };

    struct TerrainFileAccess
    {
        std::function<std::optional<TerrainFileLocation>(const std::string&)> locate;
        std::function<std::vector<uint8_t>(const std::string&)> readBytes;
        std::function<bool(const std::string&)> exists;
        std::function<bool()> isArchiveMode;
    };

    VF_TERRAIN_API bool setTerrainFileAccess(TerrainFileAccess access);
    VF_TERRAIN_API void resetTerrainFileAccess();

    // Serialises whole-file rewrites against in-flight tile reads.
    //
    // Tile payload offsets are absolute file positions. TerrainStreamManager snapshots a
    // TileIndexEntry on the main thread and then reads the file on a std::async worker that cannot
    // be cancelled (TerrainStreamManager.cpp:504-547), so a full save, a compaction or a journal
    // replay that moves records must not overlap such a read — the worker would seek to a
    // pre-rewrite offset in a post-rewrite file and decode a different tile's bytes.
    //
    // The public TerrainSerializer read entry points hold this shared for the whole read; the write
    // entry points hold it exclusively across the replacement. It is NOT recursive: internal
    // helpers must never re-acquire it, which is why the locking lives at the API boundary only.
    VF_TERRAIN_API std::shared_mutex& terrainFileMutex();

    VF_TERRAIN_API std::optional<TerrainFileLocation> locateTerrainFile(const std::string& path);
    VF_TERRAIN_API std::vector<uint8_t> readTerrainFileBytes(const std::string& path);
    VF_TERRAIN_API bool terrainFileExists(const std::string& path);
    VF_TERRAIN_API bool terrainArchiveMode();
}
