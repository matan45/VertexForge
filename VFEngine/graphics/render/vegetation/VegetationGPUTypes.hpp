#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace render::vegetation
{
    // Per vegetation billboard instance (uploaded from CPU)
    struct GrassInstanceGPU
    {
        glm::vec4 positionAndRotation;  // xyz=world position, w=Y rotation
        glm::vec4 scaleAndDensity;      // x=height, y=width, z=density, w=windPhase
        glm::vec4 color;                // xyz=color tint, w=vegetationType (0=Grass,1=Flower,2=Bush,3=Rock)
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
