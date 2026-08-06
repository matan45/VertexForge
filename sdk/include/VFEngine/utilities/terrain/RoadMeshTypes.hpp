#pragma once

#include "RoadProfile.hpp"
#include "../resource/Types.hpp"

#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <vector>

namespace terrain
{
    // One renderable piece of road. Vertices are stored RELATIVE to `origin` (the owning terrain
    // tile's world corner) — the spawned entity's transform carries the origin, which keeps float
    // precision sane on large worlds and gives the GPU cull a tight per-submesh AABB
    // (GPUObjectStreamManager.cpp:326-355 takes obj.aabbMin/Max from the SUBMESH location).
    struct RoadChunk
    {
        glm::ivec2 tile{0};
        glm::vec3 origin{0.0f};
        uint32_t ringCount = 0; // rings at LOD0
        std::array<resource::LODLevel, resource::LOD_LEVEL_COUNT> lods;
    };

    struct RoadMeshData
    {
        std::vector<RoadChunk> chunks;
        float totalLength = 0.0f;   // arc length of the generated centreline, metres
        float minTurnRadius = 0.0f; // 0 = the road is straight everywhere
        bool clamped = false;       // a corner was tighter than the profile and got pinched
        bool valid = false;
    };
}
