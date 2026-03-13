#pragma once

#include <glm/glm.hpp>
#include <cstdint>

namespace render::gpudriven
{
    constexpr uint32_t MAX_GPU_BILLBOARDS = 131072;

    struct alignas(16) BillboardInstanceGPU
    {
        glm::vec4 positionAndScale;    // xyz = world position, w = uniform scale
        glm::vec4 atlasUVRect;         // xy = UV offset, zw = UV size
        glm::vec4 colorTint;           // rgba
        uint32_t bindlessTextureIndex; // Index into bindless texture array
        uint32_t flags;                // Bit 0: axisAligned (Y-up) vs full camera-facing
        uint32_t entityId;
        float rotation;                // Z-axis rotation in radians
        glm::vec2 size;                // width, height in world units
        uint32_t padding[2];
    };
    static_assert(sizeof(BillboardInstanceGPU) == 80);

    struct BillboardRenderStats
    {
        uint32_t totalInstances = 0;
        uint32_t visibleInstances = 0;
        uint32_t culledByFrustum = 0;
        uint32_t culledByDistance = 0;
    };
}
