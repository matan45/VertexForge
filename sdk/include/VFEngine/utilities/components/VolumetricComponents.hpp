#pragma once
#include <glm/glm.hpp>
#include <cstdint>
#include "../asset/AssetRef.hpp"

namespace components
{
    // A volume flying agents navigate (VK-1660). The volume is the axis-aligned box
    // [worldPosition + centerOffset - halfExtents, worldPosition + centerOffset + halfExtents] around the entity's
    // WORLD position; the entity's rotation and scale do not affect it.
    struct VolumetricNavVolumeComponent
    {
        glm::vec3 centerOffset{0.0f};
        glm::vec3 halfExtents{25.0f, 10.0f, 25.0f};
        float voxelSize = 1.0f;
        uint8_t connectivity = 26;    // 6 or 26
        float agentClearance = 0.5f;  // free space kept around geometry (the largest agent radius that flies here)
        bool includeTerrain = true;
        bool includeStaticMeshes = true; // meshes and nav-contributing foliage
        bool includeColliders = true;
        asset::AssetRef bakedRef;     // the .vfVolNav this volume was last saved to (invalid = never saved)

        // Runtime (not serialized)
        bool isBaked = false;         // a baked grid for this volume is loaded
    };

    // A flying agent (VK-1660): it follows paths through the voxel volumes instead of the navmesh.
    struct VolumetricAgentComponent
    {
        float radius = 0.5f;
        float maxSpeed = 5.0f;
        float maxAcceleration = 10.0f;
        float stoppingDistance = 0.5f; // arrives within this 3D distance of its destination
        float turnSpeed = 360.0f;      // degrees per second toward the travel direction (0 = instant)
        bool alignPitch = false;       // also pitch toward the travel direction (else yaw only)

        // Runtime (not serialized)
        bool isActive = false;         // registered with the agent manager
    };
}
