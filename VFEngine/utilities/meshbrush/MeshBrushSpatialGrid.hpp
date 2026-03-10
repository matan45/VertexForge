#pragma once

#include <unordered_map>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>

namespace meshbrush
{
    struct IVec2Hash
    {
        size_t operator()(const glm::ivec2& v) const
        {
            size_t h1 = std::hash<int>{}(v.x);
            size_t h2 = std::hash<int>{}(v.y);
            return h1 ^ (h2 << 16);
        }
    };

    struct IVec2Equal
    {
        bool operator()(const glm::ivec2& a, const glm::ivec2& b) const
        {
            return a.x == b.x && a.y == b.y;
        }
    };

    struct SpatialEntry
    {
        uint64_t entityId = 0;
        glm::vec3 position;
    };

    class MeshBrushSpatialGrid
    {
    private:
        float cellSize = 2.0f;
        std::unordered_map<glm::ivec2, std::vector<SpatialEntry>, IVec2Hash, IVec2Equal> cells;
        std::unordered_map<uint64_t, glm::ivec2> entityToCell;

    public:
        MeshBrushSpatialGrid() = default;

        void setCellSize(float size);
        void clear();

        void insert(uint64_t entityId, const glm::vec3& position);
        void remove(uint64_t entityId);

        std::vector<SpatialEntry> queryRadius(const glm::vec3& center, float radius) const;
        bool hasNeighborWithin(const glm::vec3& position, float minDist) const;

    private:
        glm::ivec2 toCell(const glm::vec3& pos) const;
    };
}
