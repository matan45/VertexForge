#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace render::vegetation
{
    // Per vegetation billboard instance (uploaded from CPU). 4 vec4s; the mesh/task
    // shaders index instances at stride 4 (instanceIdx * 4u).
    struct GrassInstanceGPU
    {
        glm::vec4 positionAndRotation;  // xyz=world position, w=Y rotation
        glm::vec4 scaleAndDensity;      // x=height, y=width, z=density, w=windPhase
        glm::vec4 color;                // x=texIndex(bits), y=billboardMode(bits), z=tint, w=vegetationType
        glm::vec4 normalAndPad;         // xyz=terrain normal (align-to-normal), w=pad
    };

    // Per-tile vegetation metadata for GPU
    struct VegetationTileGPUData
    {
        glm::ivec2 tileCoord;
        uint32_t grassInstanceOffset;
        uint32_t grassInstanceCount;
        uint32_t padding0;
        uint32_t padding1;
        glm::vec4 boundingSphere;       // xyz=center, w=radius
        glm::vec4 aabbMin;
        glm::vec4 aabbMax;
    };

}
