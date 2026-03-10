#include "MeshBrushSpatialGrid.hpp"
#include <cmath>
#include <algorithm>

namespace meshbrush
{
    void MeshBrushSpatialGrid::setCellSize(float size)
    {
        cellSize = std::max(size, 0.1f);
    }

    void MeshBrushSpatialGrid::clear()
    {
        cells.clear();
    }

    void MeshBrushSpatialGrid::insert(uint64_t entityId, const glm::vec3& position)
    {
        auto cell = toCell(position);
        cells[cell].push_back({entityId, position});
    }

    void MeshBrushSpatialGrid::remove(uint64_t entityId)
    {
        for (auto& [key, entries] : cells)
        {
            entries.erase(
                std::remove_if(entries.begin(), entries.end(),
                    [entityId](const SpatialEntry& e) { return e.entityId == entityId; }),
                entries.end());
        }
    }

    std::vector<SpatialEntry> MeshBrushSpatialGrid::queryRadius(const glm::vec3& center, float radius) const
    {
        std::vector<SpatialEntry> result;
        float radiusSq = radius * radius;

        int minX = static_cast<int>(std::floor((center.x - radius) / cellSize));
        int maxX = static_cast<int>(std::floor((center.x + radius) / cellSize));
        int minZ = static_cast<int>(std::floor((center.z - radius) / cellSize));
        int maxZ = static_cast<int>(std::floor((center.z + radius) / cellSize));

        for (int x = minX; x <= maxX; ++x)
        {
            for (int z = minZ; z <= maxZ; ++z)
            {
                auto it = cells.find(glm::ivec2(x, z));
                if (it != cells.end())
                {
                    for (const auto& entry : it->second)
                    {
                        glm::vec3 diff = entry.position - center;
                        float distSq = diff.x * diff.x + diff.z * diff.z;
                        if (distSq <= radiusSq)
                        {
                            result.push_back(entry);
                        }
                    }
                }
            }
        }

        return result;
    }

    bool MeshBrushSpatialGrid::hasNeighborWithin(const glm::vec3& position, float minDist) const
    {
        float minDistSq = minDist * minDist;

        int minX = static_cast<int>(std::floor((position.x - minDist) / cellSize));
        int maxX = static_cast<int>(std::floor((position.x + minDist) / cellSize));
        int minZ = static_cast<int>(std::floor((position.z - minDist) / cellSize));
        int maxZ = static_cast<int>(std::floor((position.z + minDist) / cellSize));

        for (int x = minX; x <= maxX; ++x)
        {
            for (int z = minZ; z <= maxZ; ++z)
            {
                auto it = cells.find(glm::ivec2(x, z));
                if (it != cells.end())
                {
                    for (const auto& entry : it->second)
                    {
                        glm::vec3 diff = entry.position - position;
                        float distSq = diff.x * diff.x + diff.z * diff.z;
                        if (distSq < minDistSq)
                        {
                            return true;
                        }
                    }
                }
            }
        }

        return false;
    }

    glm::ivec2 MeshBrushSpatialGrid::toCell(const glm::vec3& pos) const
    {
        return glm::ivec2(
            static_cast<int>(std::floor(pos.x / cellSize)),
            static_cast<int>(std::floor(pos.z / cellSize))
        );
    }
}
