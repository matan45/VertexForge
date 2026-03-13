#pragma once
#include "NavmeshData.hpp"
#include <string>
#include <vector>
#include <unordered_set>
#include <functional>

namespace navigation
{
    struct NavmeshTileIndex
    {
        uint32_t magic = NAVMESH_FILE_MAGIC;
        uint32_t version = NAVMESH_TILE_FILE_VERSION;
        types::NavmeshBakeSettings settings;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
        std::vector<NavmeshTileCoord> tileCoords;
    };

    class NavmeshTileCache
    {
    public:
        explicit NavmeshTileCache(const std::string& directory);

        bool saveTile(const NavmeshTileCoord& coord, const NavmeshTileData& data);
        bool loadTile(const NavmeshTileCoord& coord, NavmeshTileData& outData);
        bool hasTile(const NavmeshTileCoord& coord) const;
        bool removeTile(const NavmeshTileCoord& coord);

        bool saveIndex(const NavmeshTileIndex& index);
        bool loadIndex(NavmeshTileIndex& outIndex);

        void forEachTile(const std::function<void(const NavmeshTileCoord&)>& callback) const;

        [[nodiscard]] const std::string& getDirectory() const { return directory; }

    private:
        std::string directory;
        std::unordered_set<NavmeshTileCoord, NavmeshTileCoordHash> knownTiles;

        [[nodiscard]] std::string getTilePath(const NavmeshTileCoord& coord) const;
        [[nodiscard]] std::string getIndexPath() const;
    };
}
