#pragma once

// Shared per-tile spatial hash used by both vegetation::VegetationSpatialGrid and
// foliage::FoliageSpatialGrid. Both bucket entries into integer cells (floor(position / cellSize))
// and answer radius / nearest-neighbour queries; they differ ONLY in the entry payload (billboard
// paletteEntryIndex vs foliage typeIndex), captured by the EntryT / PayloadT template parameters.
//
// EntryT must be an aggregate laid out as { uint32_t instanceIndex; glm::vec3 position; <payload>; }
// — entries are built by POSITIONAL aggregate init {index, position, payload}, so the payload field
// may be named differently (and be a different width) in each EntryT. queryRadius returns the caller's
// own EntryT, so existing call sites keep reading .typeIndex / .paletteEntryIndex unchanged.

#include <unordered_map>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>

namespace spatial
{
    template <typename EntryT, typename PayloadT>
    class SpatialHashGrid
    {
    protected:
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
        std::unordered_map<glm::ivec2, std::vector<EntryT>, IVec2Hash, IVec2Equal> cells;

        glm::ivec2 toCell(const glm::vec3& pos) const
        {
            return {static_cast<int>(std::floor(pos.x / cellSize)),
                    static_cast<int>(std::floor(pos.z / cellSize))};
        }

        // Iterate the cells overlapping the minDist box and return true on the first entry within
        // minDist for which pred(entry) is true. Payload-aware predicates (e.g. layer avoidance) live
        // in the derived class, which knows EntryT's payload field name.
        template <typename Pred>
        bool hasNeighborMatching(const glm::vec3& position, float minDist, Pred pred) const
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
                        if (dx * dx + dz * dz <= minDistSq && pred(e))
                            return true;
                    }
                }
            return false;
        }

    public:
        void setCellSize(float size) { cellSize = std::max(size, 0.1f); }
        void clear() { cells.clear(); }

        void insert(uint32_t index, const glm::vec3& position, PayloadT payload = PayloadT{})
        {
            cells[toCell(position)].push_back(EntryT{index, position, payload});
        }

        std::vector<EntryT> queryRadius(const glm::vec3& center, float radius) const
        {
            std::vector<EntryT> result;
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
            return hasNeighborMatching(position, minDist, [](const EntryT&) { return true; });
        }

        // Count neighbours within radius (for density-target painting).
        uint32_t countWithin(const glm::vec3& position, float radius) const
        {
            uint32_t count = 0;
            const float radiusSq = radius * radius;
            const int minX = static_cast<int>(std::floor((position.x - radius) / cellSize));
            const int maxX = static_cast<int>(std::floor((position.x + radius) / cellSize));
            const int minZ = static_cast<int>(std::floor((position.z - radius) / cellSize));
            const int maxZ = static_cast<int>(std::floor((position.z + radius) / cellSize));
            for (int z = minZ; z <= maxZ; ++z)
                for (int x = minX; x <= maxX; ++x)
                {
                    auto it = cells.find({x, z});
                    if (it == cells.end()) continue;
                    for (const auto& e : it->second)
                    {
                        const float dx = e.position.x - position.x;
                        const float dz = e.position.z - position.z;
                        if (dx * dx + dz * dz <= radiusSq) ++count;
                    }
                }
            return count;
        }
    };
}
