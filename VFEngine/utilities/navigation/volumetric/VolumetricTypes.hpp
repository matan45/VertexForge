#pragma once
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>
#include <functional>

namespace volumetric
{
    struct VoxelCoord
    {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;

        bool operator==(const VoxelCoord& other) const
        {
            return x == other.x && y == other.y && z == other.z;
        }

        bool operator!=(const VoxelCoord& other) const
        {
            return !(*this == other);
        }
    };

    struct VoxelCoordHash
    {
        size_t operator()(const VoxelCoord& c) const
        {
            size_t h1 = std::hash<int32_t>{}(c.x);
            size_t h2 = std::hash<int32_t>{}(c.y);
            size_t h3 = std::hash<int32_t>{}(c.z);
            return h1 ^ (h2 * 2654435761u) ^ (h3 * 40503u);
        }
    };

    struct VolumePath
    {
        std::vector<glm::vec3> waypoints;
        bool isPartial = false;
        bool isValid = false;

        glm::vec3 getBestPosition() const
        {
            if (waypoints.empty())
            {
                return glm::vec3(0.0f);
            }
            return waypoints.back();
        }

        bool hasResults() const
        {
            return !waypoints.empty();
        }
    };

    enum class VolumetricBakeStatus : uint8_t
    {
        Idle,
        Voxelizing,
        BuildingOctree,
        Complete,
        Failed
    };

    struct VolumetricBakeProgress
    {
        VolumetricBakeStatus status = VolumetricBakeStatus::Idle;
        float progress = 0.0f;
        std::string currentStage;
    };

    enum class VoxelConnectivity : uint8_t
    {
        Six = 6,
        TwentySix = 26
    };
}
