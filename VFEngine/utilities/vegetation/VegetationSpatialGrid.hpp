#pragma once

#include "VegetationTypes.hpp"
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cmath>
#include <glm/glm.hpp>

namespace vegetation
{
    struct VegSpatialEntry
    {
        uint32_t instanceIndex = 0;
        glm::vec3 position;
    };

    class VegetationSpatialGrid
    {
    private:
        struct IVec2Hash
        {
            size_t operator()(const glm::ivec2& v) const
            {
                return std::hash<int>{}(v.x) ^ (std::hash<int>{}(v.y) << 16);
            }
        };

        struct IVec2Equal
        {
            bool operator()(const glm::ivec2& a, const glm::ivec2& b) const
            {
                return a.x == b.x && a.y == b.y;
            }
        };

        float cellSize = 1.0f;
        std::unordered_map<glm::ivec2, std::vector<VegSpatialEntry>, IVec2Hash, IVec2Equal> cells;

    public:
        VegetationSpatialGrid() = default;

        void setCellSize(float size) { cellSize = std::max(size, 0.1f); }

        void clear() { cells.clear(); }

        void rebuild(const std::vector<BillboardInstance>& instances)
        {
            cells.clear();
            for (uint32_t i = 0; i < static_cast<uint32_t>(instances.size()); ++i)
            {
                insert(i, instances[i].position);
            }
        }

        void insert(uint32_t index, const glm::vec3& position)
        {
            glm::ivec2 cell = toCell(position);
            cells[cell].push_back({index, position});
        }

        std::vector<VegSpatialEntry> queryRadius(const glm::vec3& center, float radius) const
        {
            std::vector<VegSpatialEntry> result;
            float radiusSq = radius * radius;
            int minX = static_cast<int>(std::floor((center.x - radius) / cellSize));
            int maxX = static_cast<int>(std::floor((center.x + radius) / cellSize));
            int minZ = static_cast<int>(std::floor((center.z - radius) / cellSize));
            int maxZ = static_cast<int>(std::floor((center.z + radius) / cellSize));

            for (int z = minZ; z <= maxZ; ++z)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    auto it = cells.find({x, z});
                    if (it == cells.end()) continue;
                    for (const auto& entry : it->second)
                    {
                        float dx = entry.position.x - center.x;
                        float dz = entry.position.z - center.z;
                        if (dx * dx + dz * dz <= radiusSq)
                        {
                            result.push_back(entry);
                        }
                    }
                }
            }
            return result;
        }

        bool hasNeighborWithin(const glm::vec3& position, float minDist) const
        {
            float minDistSq = minDist * minDist;
            int minX = static_cast<int>(std::floor((position.x - minDist) / cellSize));
            int maxX = static_cast<int>(std::floor((position.x + minDist) / cellSize));
            int minZ = static_cast<int>(std::floor((position.z - minDist) / cellSize));
            int maxZ = static_cast<int>(std::floor((position.z + minDist) / cellSize));

            for (int z = minZ; z <= maxZ; ++z)
            {
                for (int x = minX; x <= maxX; ++x)
                {
                    auto it = cells.find({x, z});
                    if (it == cells.end()) continue;
                    for (const auto& entry : it->second)
                    {
                        float dx = entry.position.x - position.x;
                        float dz = entry.position.z - position.z;
                        if (dx * dx + dz * dz <= minDistSq)
                            return true;
                    }
                }
            }
            return false;
        }

    private:
        glm::ivec2 toCell(const glm::vec3& pos) const
        {
            return {static_cast<int>(std::floor(pos.x / cellSize)),
                    static_cast<int>(std::floor(pos.z / cellSize))};
        }
    };
}
