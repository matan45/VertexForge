#pragma once

#include <glm/glm.hpp>
#include <cmath>
#include <cstdint>
#include <utility>

namespace water
{
    // VK-1604: CPU mirror of how water.glsl samples the FFT displacement maps — a repeat-wrapped
    // bilinear fetch of a resolution x resolution grid over one patch. Extracted out of
    // render::water::OceanFFTReadback so the wrap/filter behaviour is unit-testable without a GPU
    // and so the hex-tiling height path and the readback share ONE implementation instead of two
    // that can drift apart.
    //
    // Note this cannot be bit-identical to the GPU: the texture unit filters with fixed-point
    // subtexel weights (vendor-defined precision) while this is full float32. The two agree to
    // well within wave amplitude, which is what buoyancy parity needs.

    // Wrap an integer texel coordinate into [0, resolution) the way GL_REPEAT does.
    inline uint32_t wrapTexel(int coord, uint32_t resolution)
    {
        const int n = static_cast<int>(resolution);
        return static_cast<uint32_t>(((coord % n) + n) % n);
    }

    // Bilinear sample of one channel of a resolution x resolution RGBA grid at a normalized UV.
    // UV wraps, matching the repeat sampler the shader uses. `fetch` receives a linear texel index.
    template <typename Fetch>
    float sampleBilinearWrapped(Fetch&& fetch, uint32_t resolution, glm::vec2 uv)
    {
        if (resolution == 0)
            return 0.0f;

        // Wrap into [0,1) first so large world coordinates stay well-conditioned.
        uv.x -= std::floor(uv.x);
        uv.y -= std::floor(uv.y);

        const float fx = uv.x * static_cast<float>(resolution) - 0.5f;
        const float fy = uv.y * static_cast<float>(resolution) - 0.5f;

        const int x0 = static_cast<int>(std::floor(fx));
        const int y0 = static_cast<int>(std::floor(fy));
        const float fracX = fx - static_cast<float>(x0);
        const float fracY = fy - static_cast<float>(y0);

        const uint32_t x0w = wrapTexel(x0, resolution);
        const uint32_t x1w = wrapTexel(x0 + 1, resolution);
        const uint32_t y0w = wrapTexel(y0, resolution);
        const uint32_t y1w = wrapTexel(y0 + 1, resolution);

        const float v00 = fetch(y0w * resolution + x0w);
        const float v10 = fetch(y0w * resolution + x1w);
        const float v01 = fetch(y1w * resolution + x0w);
        const float v11 = fetch(y1w * resolution + x1w);

        const float v0 = v00 + fracX * (v10 - v00);
        const float v1 = v01 + fracX * (v11 - v01);

        return v0 + fracY * (v1 - v0);
    }

    // World XZ -> patch UV, the same mapping water.glsl uses (worldPos.xz / patchSize).
    inline glm::vec2 patchUV(const glm::vec2& worldXZ, float patchSize)
    {
        if (patchSize <= 0.0f)
            return glm::vec2(0.0f);
        return worldXZ / patchSize;
    }

    // World XZ -> band height, guarding the degenerate patch size that patchUV cannot express.
    //
    // patchUV substitutes uv = (0,0) for a non-positive patch size, which sampleBilinearWrapped
    // would happily interpolate into a real (non-zero) texel value - the same value at EVERY world
    // position, i.e. a constant offset surface rather than "no height". A band with patchSize 0 has
    // no mapping from world space at all, so the only correct answer is 0. This is the guard
    // OceanFFTReadback::sampleHeightAt used to carry inline before the VK-1604 extraction.
    template <typename Fetch>
    float sampleBandHeight(Fetch&& fetch, uint32_t resolution, const glm::vec2& worldXZ, float patchSize)
    {
        if (patchSize <= 0.0f)
            return 0.0f;
        return sampleBilinearWrapped(std::forward<Fetch>(fetch), resolution, patchUV(worldXZ, patchSize));
    }
}
