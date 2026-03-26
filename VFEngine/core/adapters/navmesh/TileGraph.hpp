#pragma once

#include "navigation/NavmeshData.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>
#include <cmath>
#include <algorithm>
#include <limits>

class dtNavMesh;

namespace core
{
    class TileGraph
    {
    public:
        void rebuild(const dtNavMesh* navMesh, float tileWorldSize);
        void addOrUpdateTile(const dtNavMesh* navMesh, int tx, int tz, float tileWorldSize);
        void removeTile(int tx, int tz);
        void clear();

        std::vector<navigation::NavmeshTileCoord> findTilePath(
            const navigation::NavmeshTileCoord& start,
            const navigation::NavmeshTileCoord& end) const;

        bool hasTile(const navigation::NavmeshTileCoord& coord) const
        {
            return nodes.count(coord) > 0;
        }

        size_t nodeCount() const { return nodes.size(); }

    private:
        std::unordered_map<navigation::NavmeshTileCoord,
                           navigation::TileGraphNode,
                           navigation::NavmeshTileCoordHash> nodes;

        void removeReverseEdges(const navigation::NavmeshTileCoord& from);

        static float heuristic(const navigation::TileGraphNode& a,
                                const navigation::TileGraphNode& b)
        {
            float dx = a.center.x - b.center.x;
            float dz = a.center.z - b.center.z;
            return std::sqrt(dx * dx + dz * dz);
        }
    };
}
