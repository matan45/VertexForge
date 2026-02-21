#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include "../types/NavmeshTypes.hpp"

namespace navigation
{
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

    constexpr uint32_t NAVMESH_FILE_MAGIC = 0x564E4D53; // "VNMS"
    constexpr uint32_t NAVMESH_FILE_VERSION = 1;

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
}
