#pragma once
#include "EntityHandle.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <string>

// VK-1660: data carried by events::volumetric (services/events/volumetric/VolumetricNavEvents.hpp).
// Header-only value types.
namespace services
{
    // The editable VolumetricNavVolumeComponent fields, plus read-only bake state.
    struct VolumetricNavVolumeData
    {
        glm::vec3 centerOffset{0.0f};
        glm::vec3 halfExtents{25.0f, 10.0f, 25.0f};
        float voxelSize = 1.0f;
        int connectivity = 26;
        float agentClearance = 0.5f;
        bool includeTerrain = true;
        bool includeStaticMeshes = true;
        bool includeColliders = true;

        // Read-only
        bool baked = false;          // a grid for this volume is loaded
        bool stale = false;          // the loaded grid no longer matches the volume's box or settings
        glm::ivec3 dims{0};          // of the loaded grid
        int64_t voxels = 0;
        float navigablePercent = 0.0f;
        std::string bakedFile;       // the resolved bakedRef ("" = never saved)
    };

    // The editable VolumetricAgentComponent fields.
    struct VolumetricAgentData
    {
        float radius = 0.5f;
        float maxSpeed = 5.0f;
        float maxAcceleration = 10.0f;
        float stoppingDistance = 0.5f;
        float turnSpeed = 360.0f;
        bool alignPitch = false;
        bool registered = false; // read-only: the agent manager drives it (Play)
    };

    enum class VolumetricAgentMoveState : uint8_t
    {
        Idle,
        Moving,
        Arrived,
        Failed
    };

    struct VolumetricAgentState
    {
        VolumetricAgentMoveState state = VolumetricAgentMoveState::Idle;
        // Failed: the path status that refused the move (volumetric::volumePathStatusName) or "Stuck".
        std::string failReason;
        bool partial = false;        // the current path ends short of the destination (replanned on arrival)
        glm::vec3 destination{0.0f};
        float remaining = 0.0f;      // path length left (m)
        uint64_t volumeId = 0;
    };

    enum class VolumetricBakeState : uint8_t
    {
        Unknown = 0, // no such ticket
        Running,
        Succeeded,
        Failed,
        Cancelled
    };

    struct VolumetricBakeTicket
    {
        bool accepted = false;
        uint64_t ticket = 0; // non-zero when accepted
        std::string error;
    };

    struct VolumetricBakeOutcome
    {
        uint64_t ticket = 0;
        EntityHandle volume;
        VolumetricBakeState state = VolumetricBakeState::Unknown;
        float progress = 0.0f; // 0-1
        std::string message;   // Failed: why
        glm::ivec3 dims{0};    // Succeeded
        int64_t voxels = 0;
        int64_t navigable = 0;
        double durationMs = 0.0;
    };

    struct VolumetricVolumeStatus
    {
        EntityHandle entity;
        std::string name;
        uint64_t uuid = 0;
        glm::vec3 worldMin{0.0f}; // the volume's box now
        glm::vec3 worldMax{0.0f};
        float voxelSize = 1.0f;
        int connectivity = 26;
        float agentClearance = 0.5f;
        glm::ivec3 dims{0};       // of the loaded grid
        bool baked = false;
        bool stale = false;
        float navigablePercent = 0.0f;
        std::string file;         // the resolved bakedRef
        VolumetricBakeState bakeState = VolumetricBakeState::Unknown; // of its latest bake this session
        float progress = 0.0f;
    };

    struct VolumetricSaveResult
    {
        bool success = false;
        std::string error;
        std::string path; // the absolute path written
    };
}
