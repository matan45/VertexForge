#pragma once
#include <algorithm>
#include <cmath>

// VK-1614 terrain local wetness / snow — CPU mirror of resources/shaders/common/terrain_weather.glsl.
//
// The shading runs on the GPU, in the live terrain fragment shader only (after the RVT-resolve /
// live-composite join, which is what keeps baked RVT pages weather-independent). This header holds
// the constants the uploaded per-layer scalars are clamped against, plus a mirror of the exact
// arithmetic — side by side so the two cannot drift unnoticed, and so test_terrain_weather_response.cpp
// can assert the bit-identity properties as executable checks rather than comments.
//
// Deliberately dependency-free (<algorithm>/<cmath>) so Editor, Graphics and the CPU-only Tests
// project can all include it. Same pattern as TerrainHeightBlend.hpp / TerrainHexTiling.hpp.
//
// THE LOAD-BEARING ENCODING RULE: 0.0f means "this layer did not opt in".
// TerrainLayerGPUData::layerPorosity / layerSnowRetention are value-initialised to 0 for every slot
// (GPUDrivenRendererTerrain.cpp does `gpuLayer = {}`), and a per-tile palette index may legitimately
// exceed activeLayerCount (see TerrainLayerVisibility.hpp), so an all-zero layer MUST read as
// neutral. A "1.0 = retains snow" encoding would have made every pre-VK-1614 material — and every
// unused palette slot — silently shed all snow.

namespace terrain
{
    // Authored per-layer defaults, used when a layer opts into weather response.
    // Porosity 0.5 sits between the derived roughness*roughness of a rough ground layer (~0.81) and
    // a sealed surface; snow retention 1.0 is exactly today's behaviour.
    inline constexpr float DEFAULT_LAYER_POROSITY = 0.5f;
    inline constexpr float DEFAULT_LAYER_SNOW_RETENTION = 1.0f;

    // Authored values are clamped INTO [MIN, 1] rather than [0, 1] so that literal 0.0f stays
    // reserved as the "did not opt in" sentinel. 1/255 is below one 8-bit quantisation step, so an
    // artist who drags a slider to zero gets a value that is visually indistinguishable from zero
    // while remaining distinguishable from the sentinel.
    inline constexpr float MIN_LAYER_WEATHER_SCALAR = 1.0f / 255.0f;
    inline constexpr float MAX_LAYER_WEATHER_SCALAR = 1.0f;

    // A layer counts as authored above this. Mirrors GLSL step(WEATHER_AUTHORED_EPS, value), and is
    // an order of magnitude below MIN_LAYER_WEATHER_SCALAR so the clamp above can never land a
    // genuinely-authored value on the wrong side of it.
    inline constexpr float WEATHER_AUTHORED_EPS = 1e-6f;

    // Where standing water can form. Water pools on near-horizontal ground: cos(~21.5 deg) to
    // cos(~10 deg) against world +Y, measured on the GEOMETRIC normal so a detail normal map cannot
    // move a puddle.
    inline constexpr float PUDDLE_SLOPE_MIN = 0.93f;
    inline constexpr float PUDDLE_SLOPE_MAX = 0.985f;

    // How much water is allowed to pool on ground that is not concave at all. The concavity signal
    // is (1 - ao) — the same baked/composited AO the shader already has in a register, which is the
    // "crevice" term the UE5 bar refers to. At 0.0 only crevices would ever puddle, which reads as
    // noise; at 1.0 the term is inert and flat ground floods uniformly.
    inline constexpr float PUDDLE_OPENNESS = 0.25f;

    // Standing water is a near-mirror dielectric, NOT merely damp ground. Driving wetness to 1
    // through the wetness response alone yields roughness ~0.24 and a 30% normal flatten, which
    // reads as wet soil; a puddle needs a flat normal and a near-zero roughness.
    inline constexpr float PUDDLE_TINT = 0.5f;
    inline constexpr float PUDDLE_ROUGHNESS = 0.03f;

    // Clamp an authored per-layer weather scalar to what the GPU struct carries.
    // `optedIn == false` returns exactly 0.0f — the sentinel — which is what makes a material that
    // never touched these controls bit-identical to the pre-VK-1614 shader.
    [[nodiscard]] inline float resolveLayerWeatherScalar(float authored, bool optedIn) noexcept
    {
        if (!optedIn)
            return 0.0f;
        return std::clamp(authored, MIN_LAYER_WEATHER_SCALAR, MAX_LAYER_WEATHER_SCALAR);
    }

    // CPU mirror of the two GLSL lines that turn the per-layer splat accumulation into one value:
    //     float t = authSum * invW;                        // authored coverage share, [0, 1]
    //     float v = sum / max(authSum, 1e-6);              // weighted mean over AUTHORED layers only
    //     result  = mix(defaultValue, v, t);
    //
    // `sum` is Sum(w_i * scalar_i) and `authSum` is Sum(w_i * step(eps, scalar_i)) over the splat
    // channels that survived the composite's own `w < 0.001` cull; `invW` is 1 / max(totalW, 0.001),
    // the composite's own guard.
    //
    // Contract, asserted by the tests: with `authSum == 0.0f` this returns `defaultValue` with an
    // IDENTICAL bit pattern for every finite non-negative default — mix collapses to
    // `defaultValue * 1.0f + v * 0.0f`, and v is finite because of the max() guard on the divisor.
    //
    // Why a mix and not the algebraically equivalent "treat unauthored layers as contributing the
    // default" form `(sum + defaultValue * (totalW - authSum)) * invW`: that form computes
    // defaultValue * totalW / totalW, which is NOT bitwise defaultValue. Only the mix is exact.
    [[nodiscard]] inline float blendWeatherResponse(float sum, float authSum, float invW,
                                                   float defaultValue) noexcept
    {
        const float t = authSum * invW;
        const float v = sum / (std::max)(authSum, WEATHER_AUTHORED_EPS);
        // GLSL mix(x, y, a) == x * (1 - a) + y * a.
        return defaultValue * (1.0f - t) + v * t;
    }

    // CPU mirror of terrainWeatherOr(): the probabilistic OR of a global weather scalar and a local
    // mask sample.
    //
    // Chosen over max(g, m) and clamp(g + m):
    //   * max() makes a painted basin INVISIBLE while it rains (max(0.6, 0.3) == 0.6), which fails
    //     the crevice-puddle half of the story outright;
    //   * clamp(g + m) clips flat at the top, so a painted patch stops reading as wetter than its
    //     surroundings exactly when the weather is most interesting;
    //   * this form is saturating BY CONSTRUCTION for inputs in [0, 1] — no clamp instruction, so
    //     nothing can perturb the identity below — and is monotone non-decreasing in both arguments.
    //
    // Contract, asserted by the tests: `mask == 0.0f` returns `global` with an IDENTICAL bit pattern
    // for every finite global (g + 0.0f * (1 - g) == g + 0.0f == g). That is precisely the property
    // an absent or all-black default mask .vfImage depends on.
    [[nodiscard]] inline float combineWeatherSignal(float global, float mask) noexcept
    {
        return global + mask * (1.0f - global);
    }

    // CPU mirror of terrainPuddleCoverage(). Every input is already in a register at the apply
    // point, so this costs no texture fetch:
    //   * `upDotY`   dot(normalize(geometric world normal), +Y) — water needs flat ground;
    //   * `ao`       the composited/baked AO, live in BOTH the RVT-resolved and fallback paths, so
    //                (1 - ao) is a free concavity signal;
    //   * `porosity` the blended per-layer value — sand drinks the water instead of pooling it.
    //                This is what makes the per-layer half of the story load-bearing rather than a
    //                spare knob.
    //
    // All four factors lie in [0, 1], so the product does too and needs no clamp. Monotone
    // increasing in wetness, flatness and concavity; monotone decreasing in porosity.
    [[nodiscard]] inline float puddleCoverage(float wet, float upDotY, float ao,
                                              float porosity) noexcept
    {
        // GLSL smoothstep(e0, e1, x).
        const float t = std::clamp((upDotY - PUDDLE_SLOPE_MIN) / (PUDDLE_SLOPE_MAX - PUDDLE_SLOPE_MIN),
                                   0.0f, 1.0f);
        const float flatness = t * t * (3.0f - 2.0f * t);
        // GLSL mix(1 - ao, 1, PUDDLE_OPENNESS).
        const float cavity = (1.0f - ao) * (1.0f - PUDDLE_OPENNESS) + 1.0f * PUDDLE_OPENNESS;
        return wet * flatness * cavity * (1.0f - porosity);
    }
}
