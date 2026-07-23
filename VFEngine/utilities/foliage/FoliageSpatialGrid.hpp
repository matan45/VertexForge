#pragma once
// VK-1575: per-tile spatial hash for foliage instances. Header-only mirror of
// vegetation::VegetationSpatialGrid, keyed by the foliage typeIndex instead of the
// billboard paletteEntryIndex, so the foliage brush has no vegetation dependency.
// Used for spacing rejection during a stroke and for radius queries during erase.
#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>

namespace foliage
{
    struct FoliageSpatialEntry
    {
        uint32_t  instanceIndex = 0;   // index into the tile's foliageInstances vector
        glm::vec3 position{0.0f};
        uint16_t  typeIndex = 0;       // foliage type (for select/erase-by-type)
    };

    class FoliageSpatialGrid
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
        std::unordered_map<glm::ivec2, std::vector<FoliageSpatialEntry>, IVec2Hash, IVec2Equal> cells;

    public:
        FoliageSpatialGrid() = default;

        void setCellSize(float size) { cellSize = std::max(size, 0.1f); }
        void clear() { cells.clear(); }

        void insert(uint32_t index, const glm::vec3& position, uint16_t typeIndex = 0)
        {
            cells[toCell(position)].push_back({index, position, typeIndex});
        }

        std::vector<FoliageSpatialEntry> queryRadius(const glm::vec3& center, float radius) const
        {
            std::vector<FoliageSpatialEntry> result;
            const float radiusSq = radius * radius;
            const int minX = static_cast<int>(std::floor((center.x - radius) / cellSize));
            const int maxX = static_cast<int>(std::floor((center.x + radius) / cellSize));
            const int minZ = static_cast<int>(std::floor((center.z - radius) / cellSize));
            const int maxZ = static_cast<int>(std::floor((center.z + radius) / cellSize));
            for (int z = minZ; z <= maxZ; ++z)
                for (int x = minX; x <= maxX; ++x)
                {
                    auto it = cells.find({x, z});
                    if (it == cells.end()) continue;
                    for (const auto& e : it->second)
                    {
                        const float dx = e.position.x - center.x;
                        const float dz = e.position.z - center.z;
                        if (dx * dx + dz * dz <= radiusSq) result.push_back(e);
                    }
                }
            return result;
        }

        bool hasNeighborWithin(const glm::vec3& position, float minDist) const
        {
            const float minDistSq = minDist * minDist;
            const int minX = static_cast<int>(std::floor((position.x - minDist) / cellSize));
            const int maxX = static_cast<int>(std::floor((position.x + minDist) / cellSize));
            const int minZ = static_cast<int>(std::floor((position.z - minDist) / cellSize));
            const int maxZ = static_cast<int>(std::floor((position.z + minDist) / cellSize));
            for (int z = minZ; z <= maxZ; ++z)
                for (int x = minX; x <= maxX; ++x)
                {
                    auto it = cells.find({x, z});
                    if (it == cells.end()) continue;
                    for (const auto& e : it->second)
                    {
                        const float dx = e.position.x - position.x;
                        const float dz = e.position.z - position.z;
                        if (dx * dx + dz * dz <= minDistSq) return true;
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
