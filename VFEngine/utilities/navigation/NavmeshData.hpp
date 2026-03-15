#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <functional>
#include "../types/NavmeshTypes.hpp"

namespace navigation
{
    struct NavmeshTileCoord
    {
        int32_t x = 0;
        int32_t z = 0;

        bool operator==(const NavmeshTileCoord& other) const
        {
            return x == other.x && z == other.z;
        }

        bool operator!=(const NavmeshTileCoord& other) const
        {
            return !(*this == other);
        }
    };

    struct NavmeshTileCoordHash
    {
        size_t operator()(const NavmeshTileCoord& c) const
        {
            size_t h1 = std::hash<int32_t>{}(c.x);
            size_t h2 = std::hash<int32_t>{}(c.z);
            return h1 ^ (h2 * 2654435761u);
        }
    };

    struct NavmeshInputGeometry
    {
        std::vector<float> vertices;     // Flat: x,y,z,x,y,z,...
        std::vector<int> triangles;      // Index triplets
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};

        void addVertex(const glm::vec3& v)
        {
            if (vertices.empty())
            {
                boundsMin = v;
                boundsMax = v;
            }
            else
            {
                boundsMin = glm::min(boundsMin, v);
                boundsMax = glm::max(boundsMax, v);
            }
            vertices.push_back(v.x);
            vertices.push_back(v.y);
            vertices.push_back(v.z);
        }

        void addTriangle(int a, int b, int c)
        {
            triangles.push_back(a);
            triangles.push_back(b);
            triangles.push_back(c);
        }

        int getVertexCount() const { return static_cast<int>(vertices.size() / 3); }
        int getTriangleCount() const { return static_cast<int>(triangles.size() / 3); }
        bool isEmpty() const { return vertices.empty() || triangles.empty(); }
    };

    struct NavPath
    {
        std::vector<glm::vec3> waypoints;
        bool isPartial = false;
        bool isValid = false;
    };

    struct NavmeshRaycastResult
    {
        bool hit = false;
        glm::vec3 hitPoint{0.0f};
        float hitDistance = 0.0f;
    };

    constexpr uint32_t NAVMESH_FILE_MAGIC = 0x564E4D53; // "VNMS"
    constexpr uint32_t NAVMESH_FILE_VERSION = 1;
    constexpr uint32_t NAVMESH_TILE_FILE_VERSION = 2;

    struct NavmeshFileHeader
    {
        uint32_t magic = NAVMESH_FILE_MAGIC;
        uint32_t version = NAVMESH_FILE_VERSION;
        types::NavmeshBakeSettings settings;
        uint32_t tileCount = 0;
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
    };

    struct NavmeshTileData
    {
        int32_t x = 0;
        int32_t y = 0;
        uint32_t dataSize = 0;
        std::vector<uint8_t> data;
    };

    struct NavmeshTileBounds
    {
        glm::vec3 min{0.0f};
        glm::vec3 max{0.0f};
    };

    inline NavmeshTileBounds computeTileBounds(const NavmeshTileCoord& coord,
                                                const types::NavmeshBakeSettings& settings,
                                                float yMin, float yMax)
    {
        float tileWorldSize = settings.tileSize * settings.cellSize;
        NavmeshTileBounds bounds;
        bounds.min = glm::vec3(coord.x * tileWorldSize, yMin, coord.z * tileWorldSize);
        bounds.max = glm::vec3((coord.x + 1) * tileWorldSize, yMax, (coord.z + 1) * tileWorldSize);
        return bounds;
    }

    inline NavmeshTileBounds expandBoundsForOverlap(const NavmeshTileBounds& bounds,
                                                     const types::NavmeshBakeSettings& settings)
    {
        float borderExpand = settings.agentRadius + settings.cellSize * 3.0f;
        NavmeshTileBounds expanded;
        expanded.min = bounds.min - glm::vec3(borderExpand, 0.0f, borderExpand);
        expanded.max = bounds.max + glm::vec3(borderExpand, 0.0f, borderExpand);
        return expanded;
    }
}
