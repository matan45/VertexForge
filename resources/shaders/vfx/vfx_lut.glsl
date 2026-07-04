#ifndef VFX_LUT_GLSL
#define VFX_LUT_GLSL

// Shared VFX LUT channel indices + flags + speed/trail remap helper.
// Mirrors render::vfx::LUTChannel / render::vfx::LUTFlags (GPUVFXTypes.hpp).
// The LUT storage buffer itself is declared per-shader (different bindings:
// compute sim = binding 4, ribbon render = binding 7), as is the sampleLUT()
// fetch that indexes it, so only the layout constants + helper live here.

const uint LUT_CH_COLOR = 0u;
const uint LUT_CH_SIZE = 1u;
const uint LUT_CH_SPEED = 2u;
const uint LUT_CH_ROTATION = 3u;
const uint LUT_CH_GLOW = 4u;
const uint LUT_CH_SIZE_BY_SPEED = 5u;          // VK-1473
const uint LUT_CH_COLOR_BY_SPEED = 6u;         // VK-1473
const uint LUT_CH_RIBBON_WIDTH = 7u;           // VK-1474
const uint LUT_CH_RIBBON_TAIL_GRADIENT = 8u;   // VK-1474

const uint LUT_FLAG_COLOR = 1u;                // 1 << LUT_CH_COLOR
const uint LUT_FLAG_SIZE = 2u;                 // 1 << LUT_CH_SIZE
const uint LUT_FLAG_SPEED = 4u;                // 1 << LUT_CH_SPEED
const uint LUT_FLAG_ROTATION = 8u;             // 1 << LUT_CH_ROTATION
const uint LUT_FLAG_GLOW = 16u;                // 1 << LUT_CH_GLOW
const uint LUT_FLAG_SIZE_BY_SPEED = 32u;       // 1 << LUT_CH_SIZE_BY_SPEED    (VK-1473)
const uint LUT_FLAG_COLOR_BY_SPEED = 64u;      // 1 << LUT_CH_COLOR_BY_SPEED   (VK-1473)
const uint LUT_FLAG_RIBBON_WIDTH = 128u;       // 1 << LUT_CH_RIBBON_WIDTH     (VK-1474)
const uint LUT_FLAG_RIBBON_TAIL_GRADIENT = 256u; // 1 << LUT_CH_RIBBON_TAIL_GRADIENT (VK-1474)

// Remap a raw value into [0,1] over [lo,hi]; degenerate (hi<=lo) -> 0.
// Mirror of vfx::normalizedSpeed01 (VFXSpeedRemap.hpp).
float vfxNormalize01(float v, float lo, float hi)
{
    return (hi - lo <= 1e-6) ? 0.0 : clamp((v - lo) / (hi - lo), 0.0, 1.0);
}

#endif // VFX_LUT_GLSL
