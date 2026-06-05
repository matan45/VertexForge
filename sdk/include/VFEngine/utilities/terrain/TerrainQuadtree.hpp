#pragma once
#include "TerrainExport.hpp"

#include "TerrainTypes.hpp"
#include "TerrainTile.hpp"
#include "math/Frustum.hpp"
#include <vector>
#include <unordered_map>
#include <memory>

namespace terrain
{
#pragma warning(push)
#pragma warning(disable: 4251)
    class VF_TERRAIN_API TerrainQuadtree
    {
    public:
        TerrainQuadtree();
        ~TerrainQuadtree() = default;

        // Bulk build from tile map (more efficient than N individual inserts)
        void rebuild(const std::unordered_map<TileCoord, std::unique_ptr<TerrainTile>, TileCoordHash>& tiles);
        void clear();

        // Incremental updates
        void insert(TerrainTile* tile);
        void remove(const TileCoord& coord);

        // Spatial queries
        void queryFrustum(const math::Frustum& frustum, float worldTileSize,
                          std::vector<TerrainTile*>& results) const;

        void queryRange(const glm::vec3& center, float radius, float worldTileSize,
                        std::vector<TerrainTile*>& results) const;

        void queryCone(const glm::vec3& apex, const glm::vec3& direction,
                       float halfAngle, float maxDistance, float worldTileSize,
                       std::vector<TerrainTile*>& results) const;

        // Cached bounds (maintained incrementally)
        void getBounds(int32_t& minX, int32_t& minZ, int32_t& maxX, int32_t& maxZ) const;
        [[nodiscard]] bool empty() const { return count == 0; }
        [[nodiscard]] size_t size() const { return count; }

    private:
        struct Node
        {
            int32_t minX = 0, minZ = 0, maxX = 0, maxZ = 0;
            float minY = 0.0f, maxY = 0.0f;
            uint32_t children[4] = {0, 0, 0, 0}; // NW, NE, SW, SE (0 = none)
            std::vector<TerrainTile*> tiles;

            bool isLeaf() const
            {
                return children[0] == 0 && children[1] == 0 &&
                       children[2] == 0 && children[3] == 0;
            }

            int32_t midX() const { return minX + (maxX - minX) / 2; }
            int32_t midZ() const { return minZ + (maxZ - minZ) / 2; }

            bool isSingleCell() const { return minX == maxX && minZ == maxZ; }
        };

        std::vector<Node> nodePool;
        uint32_t rootIndex = 0;
        size_t count = 0;

        int32_t boundsMinX = 0, boundsMinZ = 0;
        int32_t boundsMaxX = 0, boundsMaxZ = 0;

        static constexpr int MAX_DEPTH = 14;
        static constexpr int SPLIT_THRESHOLD = 4;

        uint32_t allocNode(int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ);
        void insertRecursive(uint32_t nodeIdx, TerrainTile* tile, int depth);
        bool removeRecursive(uint32_t nodeIdx, const TileCoord& coord);
        void splitNode(uint32_t nodeIdx);
        int getQuadrant(const Node& node, int32_t x, int32_t z) const;
        void expandRoot(int32_t x, int32_t z);

        void queryFrustumRecursive(uint32_t nodeIdx, const math::Frustum& frustum,
                                   float worldTileSize,
                                   std::vector<TerrainTile*>& results) const;

        void queryRangeRecursive(uint32_t nodeIdx, float centerX, float centerZ,
                                 float radiusSq, float worldTileSize,
                                 std::vector<TerrainTile*>& results) const;

        math::AABB nodeToWorldAABB(const Node& node, float worldTileSize) const;

        bool nodeIntersectsCircle(const Node& node, float cx, float cz,
                                  float radiusSq, float worldTileSize) const;

        void refitYBounds(uint32_t nodeIdx);
    };
#pragma warning(pop)

} // namespace terrain
