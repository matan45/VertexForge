#pragma once
#include "NavmeshData.hpp"
#include <string>
#include <vector>
#include <unordered_set>
#include <functional>

namespace navigation
{
    // Utilities-local mirror of events::navmesh::NavmeshStreamingConfig plus the
    // on/off flag — persisted in index.vfNavIndex so streaming is an asset-level
    // opt-in (terrain parity). Defaults keep streaming OFF (eager full load).
    struct NavmeshIndexStreamingSettings
    {
        uint8_t enabled = 0;
        float loadRadius = 512.0f;
        float unloadRadius = 640.0f;
        int32_t maxLoadsPerFrame = 2;
        int32_t maxUnloadsPerFrame = 2;
        float lodDistances[3] = {256.0f, 512.0f, 1024.0f};
    };

    struct NavmeshTileIndex
    {
        uint32_t magic = NAVMESH_FILE_MAGIC;
        uint32_t version = NAVMESH_TILE_FILE_VERSION;
        types::NavmeshBakeSettings settings;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        NavmeshIndexStreamingSettings streaming;
        std::vector<NavmeshTileCoord> tileCoords;
    };

    class NavmeshTileCache
    {
    public:
        // Reads route through these so tiles resolve from the .vfpak in shipped
        // builds (default: VirtualFileSystem) and tests can inject in-memory data.
        // Writes always target the real filesystem (editor-only) and are rejected
        // in archive mode.
        using FileReadFn = std::function<std::vector<uint8_t>(const std::string&)>;
        using FileExistsFn = std::function<bool(const std::string&)>;

        explicit NavmeshTileCache(const std::string& directory);

        void setFileAccess(FileReadFn read, FileExistsFn exists);

        // Non-LOD methods (default to LOD 0)
        bool saveTile(const NavmeshTileCoord& coord, const NavmeshTileData& data);
        bool loadTile(const NavmeshTileCoord& coord, NavmeshTileData& outData);
        bool hasTile(const NavmeshTileCoord& coord) const;
        bool removeTile(const NavmeshTileCoord& coord);

        // LOD-aware overloads
        bool saveTile(const NavmeshTileLodKey& key, const NavmeshTileData& data);
        bool loadTile(const NavmeshTileLodKey& key, NavmeshTileData& outData);
        bool hasTile(const NavmeshTileLodKey& key) const;

        bool saveIndex(const NavmeshTileIndex& index);
        bool loadIndex(NavmeshTileIndex& outIndex);

        void forEachTile(const std::function<void(const NavmeshTileCoord&)>& callback) const;

        [[nodiscard]] const std::string& getDirectory() const { return directory; }

    private:
        std::string directory;
        FileReadFn readFileFn;
        FileExistsFn fileExistsFn;
        std::unordered_set<NavmeshTileCoord, NavmeshTileCoordHash> knownTiles;
        std::unordered_set<NavmeshTileLodKey, NavmeshTileLodKeyHash> knownTileLods;

        [[nodiscard]] std::string getTilePath(const NavmeshTileCoord& coord) const;
        [[nodiscard]] std::string getTilePath(const NavmeshTileLodKey& key) const;
        [[nodiscard]] std::string getIndexPath() const;
    };
}
