#pragma once

#include <vector>
#include <cstdint>
#include <utility>
#include <cmath>
#include <algorithm>

namespace core::physics
{
    // Decimates a square height field grid for physics LOD.
    // Preserves boundary rows/columns so adjacent tiles always match.
    // Jolt requires sampleCount = power-of-2 + 1 (129, 65, 33, 17, 9).
    //
    // lodLevel 0 = full resolution (copy)
    // lodLevel 1 = 2x decimation  (129->65, 65->33, 33->17)
    // lodLevel 2 = 4x decimation  (129->33, 65->17, 33->9)
    inline std::pair<std::vector<float>, uint32_t> decimateHeightField(
        const float* fullRes, uint32_t fullSampleCount, uint8_t lodLevel)
    {
        if (lodLevel == 0 || fullSampleCount <= 3)
            return {std::vector<float>(fullRes, fullRes + static_cast<size_t>(fullSampleCount) * fullSampleCount),
                    fullSampleCount};

        uint32_t stride = 1u << lodLevel; // 2 for LOD1, 4 for LOD2
        uint32_t decimatedCount = (fullSampleCount - 1) / stride + 1;

        // Ensure we get a valid Jolt-compatible count (power-of-2 + 1)
        if (decimatedCount < 3)
            decimatedCount = 3;

        std::vector<float> result(static_cast<size_t>(decimatedCount) * decimatedCount);

        for (uint32_t dz = 0; dz < decimatedCount; ++dz)
        {
            for (uint32_t dx = 0; dx < decimatedCount; ++dx)
            {
                // Map decimated coords back to full-res coords
                uint32_t sx = std::min(dx * stride, fullSampleCount - 1);
                uint32_t sz = std::min(dz * stride, fullSampleCount - 1);

                result[static_cast<size_t>(dz) * decimatedCount + dx] =
                    fullRes[static_cast<size_t>(sz) * fullSampleCount + sx];
            }
        }

        return {std::move(result), decimatedCount};
    }

    // Estimate physics memory for a tile at a given LOD (bytes).
    // Jolt HeightFieldShape stores ~1 byte/sample + BVH overhead.
    inline size_t estimatePhysicsTileMemory(uint32_t fullSampleCount, uint8_t lodLevel)
    {
        uint32_t stride = (lodLevel == 0) ? 1u : (1u << lodLevel);
        uint32_t decimatedCount = (fullSampleCount - 1) / stride + 1;
        if (decimatedCount < 3) decimatedCount = 3;

        size_t samples = static_cast<size_t>(decimatedCount) * decimatedCount;
        // ~1 byte quantized storage per sample + ~50% overhead for BVH tree
        return static_cast<size_t>(samples * 1.5f) + 256;
    }
}
