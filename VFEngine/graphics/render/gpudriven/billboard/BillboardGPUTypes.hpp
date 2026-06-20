#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <cmath>

namespace render::gpudriven
{
    constexpr uint32_t MAX_GPU_BILLBOARDS = 131072;

    // flags bits for BillboardInstanceGPU::flags.
    constexpr uint32_t FLAG_AXIS_ALIGNED = 1u; // bit 0: cylindrical (Y-up) vs full camera-facing
    constexpr uint32_t FLAG_ANIMATED = 2u;     // bit 1: flipbook/scroll/spin animation active

    struct alignas(16) BillboardInstanceGPU
    {
        glm::vec4 positionAndScale;    // xyz = world position, w = uniform scale
        // When FLAG_ANIMATED is NOT set: xy = static UV offset, zw = static UV size.
        // When FLAG_ANIMATED IS set:     xy = scrollU/scrollV (units/sec), zw unused.
        glm::vec4 atlasUVRect;
        glm::vec4 colorTint;           // rgba
        uint32_t bindlessTextureIndex; // Index into bindless texture array
        uint32_t flags;                // see FLAG_* above
        uint32_t entityId;
        // When FLAG_ANIMATED is NOT set: static Z-axis rotation in radians.
        // When FLAG_ANIMATED IS set:     spin rate in rad/sec (time-driven in shader).
        float rotation;
        glm::vec2 size;                // width, height in world units
        // Flipbook animation (only consulted when FLAG_ANIMATED is set). The
        // flipbook sub-rect is computed in-shader from these + the push-constant
        // time, mirroring render::computeFlipbookFrame in FlipbookMath.hpp.
        float flipbookColsRows;        // encoded: floor(cols)*256.0 + rows  (see encodeFlipbookColsRows)
        float flipbookFrameRate;       // frames/sec
    };
    static_assert(sizeof(BillboardInstanceGPU) == 80);

    // Pack sprite-sheet dimensions into a single float for the GPU instance.
    // Mirror of the in-shader decode: cols = floor(v/256), rows = v - cols*256.
    // cols/rows are clamped to [0,255] so the encoding stays exact in fp32
    // (256*255 + 255 = 65535, well within fp32's 24-bit integer range).
    inline float encodeFlipbookColsRows(uint32_t cols, uint32_t rows)
    {
        const float c = static_cast<float>(cols > 255u ? 255u : cols);
        const float r = static_cast<float>(rows > 255u ? 255u : rows);
        return c * 256.0f + r;
    }

    // Inverse of encodeFlipbookColsRows. Matches the shader decode exactly.
    inline void decodeFlipbookColsRows(float encoded, uint32_t& cols, uint32_t& rows)
    {
        const float c = std::floor(encoded / 256.0f);
        const float r = encoded - c * 256.0f;
        cols = static_cast<uint32_t>(c);
        rows = static_cast<uint32_t>(r);
    }

    struct BillboardRenderStats
    {
        uint32_t totalInstances = 0;
        uint32_t visibleInstances = 0;
        uint32_t culledByFrustum = 0;
        uint32_t culledByDistance = 0;
    };
}
