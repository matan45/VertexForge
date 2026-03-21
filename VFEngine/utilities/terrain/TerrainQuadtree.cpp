#include "TerrainQuadtree.hpp"
#include <algorithm>
#include <limits>

namespace terrain
{
    TerrainQuadtree::TerrainQuadtree()
    {
        nodePool.reserve(64);
        nodePool.emplace_back();
    }

    uint32_t TerrainQuadtree::allocNode(int32_t minX, int32_t minZ, int32_t maxX, int32_t maxZ)
    {
        uint32_t idx = static_cast<uint32_t>(nodePool.size());
        nodePool.emplace_back();
        auto& node = nodePool[idx];
        node.minX = minX;
        node.minZ = minZ;
        node.maxX = maxX;
        node.maxZ = maxZ;
        node.minY = std::numeric_limits<float>::max();
        node.maxY = std::numeric_limits<float>::lowest();
        return idx;
    }

    void TerrainQuadtree::clear()
    {
        nodePool.clear();
        nodePool.emplace_back();
        rootIndex = 0;
        count = 0;
        boundsMinX = boundsMinZ = boundsMaxX = boundsMaxZ = 0;
    }

    void TerrainQuadtree::rebuild(
        const std::unordered_map<TileCoord, std::unique_ptr<TerrainTile>, TileCoordHash>& tileMap)
    {
        clear();
        if (tileMap.empty())
            return;

        auto it = tileMap.begin();
        int32_t mnX = it->first.x, mxX = it->first.x;
        int32_t mnZ = it->first.z, mxZ = it->first.z;
        for (++it; it != tileMap.end(); ++it)
        {
            mnX = std::min(mnX, it->first.x);
            mxX = std::max(mxX, it->first.x);
            mnZ = std::min(mnZ, it->first.z);
            mxZ = std::max(mxZ, it->first.z);
        }

        boundsMinX = mnX;
        boundsMinZ = mnZ;
        boundsMaxX = mxX;
        boundsMaxZ = mxZ;
        rootIndex = allocNode(mnX, mnZ, mxX, mxZ);

        for (auto& [coord, tile] : tileMap)
        {
            if (tile)
            {
                insertRecursive(rootIndex, tile.get(), 0);
                count++;
            }
        }
    }

    void TerrainQuadtree::insert(TerrainTile* tile)
    {
        if (!tile)
            return;

        int32_t x = tile->coord.x;
        int32_t z = tile->coord.z;

        if (rootIndex == 0)
        {
            rootIndex = allocNode(x, z, x, z);
            boundsMinX = boundsMaxX = x;
            boundsMinZ = boundsMaxZ = z;
        }
        else
        {
            while (x < nodePool[rootIndex].minX || x > nodePool[rootIndex].maxX ||
                   z < nodePool[rootIndex].minZ || z > nodePool[rootIndex].maxZ)
            {
                expandRoot(x, z);
            }

            boundsMinX = std::min(boundsMinX, x);
            boundsMinZ = std::min(boundsMinZ, z);
            boundsMaxX = std::max(boundsMaxX, x);
            boundsMaxZ = std::max(boundsMaxZ, z);
        }

        insertRecursive(rootIndex, tile, 0);
        count++;
    }

    void TerrainQuadtree::expandRoot(int32_t x, int32_t z)
    {
        auto& root = nodePool[rootIndex];
        int32_t sz = std::max(root.maxX - root.minX + 1, root.maxZ - root.minZ + 1) * 2;
        if (sz < 2) sz = 2;

        int32_t newMinX = (x < root.minX) ? root.maxX - sz + 1 : root.minX;
        int32_t newMinZ = (z < root.minZ) ? root.maxZ - sz + 1 : root.minZ;

        uint32_t newRootIdx = allocNode(newMinX, newMinZ, newMinX + sz - 1, newMinZ + sz - 1);
        nodePool[newRootIdx].minY = nodePool[rootIndex].minY;
        nodePool[newRootIdx].maxY = nodePool[rootIndex].maxY;

        int quadrant = getQuadrant(nodePool[newRootIdx], nodePool[rootIndex].minX, nodePool[rootIndex].minZ);
        if (quadrant >= 0)
            nodePool[newRootIdx].children[quadrant] = rootIndex;
        else
            nodePool[newRootIdx].tiles = std::move(nodePool[rootIndex].tiles);

        rootIndex = newRootIdx;
    }

    int TerrainQuadtree::getQuadrant(const Node& node, int32_t x, int32_t z) const
    {
        if (node.isSingleCell())
            return -1;

        int32_t mx = node.midX();
        int32_t mz = node.midZ();

        if (x <= mx && z <= mz) return 0;
        if (x > mx && z <= mz)  return 1;
        if (x <= mx && z > mz)  return 2;
        return 3;
    }

    void TerrainQuadtree::splitNode(uint32_t nodeIdx)
    {
        auto& node = nodePool[nodeIdx];
        if (node.isSingleCell())
            return;

        int32_t mx = node.midX();
        int32_t mz = node.midZ();

        node.children[0] = allocNode(node.minX, node.minZ, mx, mz);
        node.children[1] = allocNode(mx + 1, node.minZ, node.maxX, mz);
        node.children[2] = allocNode(node.minX, mz + 1, mx, node.maxZ);
        node.children[3] = allocNode(mx + 1, mz + 1, node.maxX, node.maxZ);

        std::vector<TerrainTile*> oldTiles = std::move(node.tiles);
        auto& nodeRef = nodePool[nodeIdx];
        nodeRef.tiles.clear();

        for (auto* tile : oldTiles)
        {
            int q = getQuadrant(nodeRef, tile->coord.x, tile->coord.z);
            if (q >= 0 && nodeRef.children[q] != 0)
            {
                auto& child = nodePool[nodeRef.children[q]];
                child.tiles.push_back(tile);
                child.minY = std::min(child.minY, tile->worldBounds.min.y);
                child.maxY = std::max(child.maxY, tile->worldBounds.max.y);
            }
            else
            {
                nodeRef.tiles.push_back(tile);
            }
        }
    }

    void TerrainQuadtree::insertRecursive(uint32_t nodeIdx, TerrainTile* tile, int depth)
    {
        auto& node = nodePool[nodeIdx];
        node.minY = std::min(node.minY, tile->worldBounds.min.y);
        node.maxY = std::max(node.maxY, tile->worldBounds.max.y);

        if (node.isLeaf())
        {
            if (static_cast<int>(node.tiles.size()) < SPLIT_THRESHOLD ||
                depth >= MAX_DEPTH || node.isSingleCell())
            {
                node.tiles.push_back(tile);
                return;
            }
            splitNode(nodeIdx);
        }

        auto& nodeRef = nodePool[nodeIdx];
        int q = getQuadrant(nodeRef, tile->coord.x, tile->coord.z);
        if (q >= 0 && nodeRef.children[q] != 0)
            insertRecursive(nodeRef.children[q], tile, depth + 1);
        else
            nodeRef.tiles.push_back(tile);
    }

    void TerrainQuadtree::remove(const TileCoord& coord)
    {
        if (rootIndex == 0)
            return;

        if (removeRecursive(rootIndex, coord))
            count--;
    }

    bool TerrainQuadtree::removeRecursive(uint32_t nodeIdx, const TileCoord& coord)
    {
        auto& node = nodePool[nodeIdx];

        for (auto it = node.tiles.begin(); it != node.tiles.end(); ++it)
        {
            if ((*it)->coord == coord)
            {
                node.tiles.erase(it);
                refitYBounds(nodeIdx);
                return true;
            }
        }

        if (!node.isLeaf())
        {
            int q = getQuadrant(node, coord.x, coord.z);
            if (q >= 0 && node.children[q] != 0)
            {
                if (removeRecursive(node.children[q], coord))
                {
                    refitYBounds(nodeIdx);
                    return true;
                }
            }
        }

        return false;
    }

    void TerrainQuadtree::refitYBounds(uint32_t nodeIdx)
    {
        auto& node = nodePool[nodeIdx];
        node.minY = std::numeric_limits<float>::max();
        node.maxY = std::numeric_limits<float>::lowest();

        for (auto* tile : node.tiles)
        {
            node.minY = std::min(node.minY, tile->worldBounds.min.y);
            node.maxY = std::max(node.maxY, tile->worldBounds.max.y);
        }

        for (int i = 0; i < 4; ++i)
        {
            if (node.children[i] != 0)
            {
                node.minY = std::min(node.minY, nodePool[node.children[i]].minY);
                node.maxY = std::max(node.maxY, nodePool[node.children[i]].maxY);
            }
        }
    }

    void TerrainQuadtree::getBounds(int32_t& outMinX, int32_t& outMinZ,
                                    int32_t& outMaxX, int32_t& outMaxZ) const
    {
        outMinX = boundsMinX;
        outMinZ = boundsMinZ;
        outMaxX = boundsMaxX;
        outMaxZ = boundsMaxZ;
    }

    math::AABB TerrainQuadtree::nodeToWorldAABB(const Node& node, float worldTileSize) const
    {
        return math::AABB(
            glm::vec3(static_cast<float>(node.minX) * worldTileSize, node.minY,
                      static_cast<float>(node.minZ) * worldTileSize),
            glm::vec3(static_cast<float>(node.maxX + 1) * worldTileSize, node.maxY,
                      static_cast<float>(node.maxZ + 1) * worldTileSize)
        );
    }

    void TerrainQuadtree::queryFrustum(const math::Frustum& frustum, float worldTileSize,
                                       std::vector<TerrainTile*>& results) const
    {
        results.clear();
        if (rootIndex == 0)
            return;

        results.reserve(128);
        queryFrustumRecursive(rootIndex, frustum, worldTileSize, results);
    }

    void TerrainQuadtree::queryFrustumRecursive(uint32_t nodeIdx, const math::Frustum& frustum,
                                                float worldTileSize,
                                                std::vector<TerrainTile*>& results) const
    {
        const auto& node = nodePool[nodeIdx];

        if (!frustum.intersectsAABB(nodeToWorldAABB(node, worldTileSize)))
            return;

        for (auto* tile : node.tiles)
        {
            if (frustum.intersectsAABB(tile->worldBounds))
                results.push_back(tile);
        }

        for (int i = 0; i < 4; ++i)
        {
            if (node.children[i] != 0)
                queryFrustumRecursive(node.children[i], frustum, worldTileSize, results);
        }
    }

    bool TerrainQuadtree::nodeIntersectsCircle(const Node& node, float cx, float cz,
                                               float radiusSq, float worldTileSize) const
    {
        float wMinX = static_cast<float>(node.minX) * worldTileSize;
        float wMinZ = static_cast<float>(node.minZ) * worldTileSize;
        float wMaxX = static_cast<float>(node.maxX + 1) * worldTileSize;
        float wMaxZ = static_cast<float>(node.maxZ + 1) * worldTileSize;

        float dx = std::clamp(cx, wMinX, wMaxX) - cx;
        float dz = std::clamp(cz, wMinZ, wMaxZ) - cz;
        return (dx * dx + dz * dz) <= radiusSq;
    }

    void TerrainQuadtree::queryRange(const glm::vec3& center, float radius, float worldTileSize,
                                     std::vector<TerrainTile*>& results) const
    {
        results.clear();
        if (rootIndex == 0)
            return;

        results.reserve(64);
        queryRangeRecursive(rootIndex, center.x, center.z, radius * radius, worldTileSize, results);
    }

    void TerrainQuadtree::queryRangeRecursive(uint32_t nodeIdx, float cx, float cz,
                                              float radiusSq, float worldTileSize,
                                              std::vector<TerrainTile*>& results) const
    {
        const auto& node = nodePool[nodeIdx];

        if (!nodeIntersectsCircle(node, cx, cz, radiusSq, worldTileSize))
            return;

        for (auto* tile : node.tiles)
        {
            glm::vec3 tileCenter = (tile->worldBounds.min + tile->worldBounds.max) * 0.5f;
            float dx = tileCenter.x - cx;
            float dz = tileCenter.z - cz;
            if (dx * dx + dz * dz <= radiusSq)
                results.push_back(tile);
        }

        for (int i = 0; i < 4; ++i)
        {
            if (node.children[i] != 0)
                queryRangeRecursive(node.children[i], cx, cz, radiusSq, worldTileSize, results);
        }
    }

    void TerrainQuadtree::queryCone(const glm::vec3&, const glm::vec3&,
                                    float, float, float,
                                    std::vector<TerrainTile*>& results) const
    {
        results.clear();
    }

} // namespace terrain
