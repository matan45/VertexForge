#pragma once

// VK-1577 — GPU-side reflection probe types.
//
// Mirrored by resources/shaders/common/reflection_probe_types.glsl. The two MUST stay in lockstep;
// the static_asserts below pin every offset so a field insertion fails the build instead of
// silently shifting what the shader reads.
//
// The CPU math that fills these fields lives in utilities/probe/ReflectionProbeMath.hpp and is
// doctested — the GLSL is a transliteration of it, never an independent implementation.

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>

namespace render::probe
{
    // Hard cap on simultaneously bound probes. This is the descriptorCount of set 0 binding 4, so
    // changing it changes a descriptor set layout — it is not a soft limit.
    //
    // 8 x (128^2 cube, 5 mips, RGBA16F) = 8 x 1'047'552 B ~= 8.0 MiB of persistent VRAM.
    inline constexpr uint32_t MAX_REFLECTION_PROBES = 8;

    // Per-probe cubemap geometry. 128^2 with 5 mips gives roughness = m/(mips-1) = m/4, which is
    // exactly the range consumers sample: textureLod(cube, R, roughness * MAX_REFLECTION_LOD) with
    // MAX_REFLECTION_LOD == 4.0 (common/lighting_functions.glsl:9). Matching the dynamic-sky capture
    // geometry is what lets ibl/sky_prefilter.glsl be reused verbatim — it hardcodes resolution 128.
    inline constexpr uint32_t PROBE_CUBE_SIZE = 128;
    inline constexpr uint32_t PROBE_CUBE_MIPS = 5;

    enum class GPUProbeShape : uint32_t
    {
        Box = 0,
        Sphere = 1
    };

    // std430 layout. Both matrices are stored so the fragment shader never calls inverse().
    struct GPUReflectionProbe
    {
        glm::mat4 worldToLocal{1.0f};       //   0  world -> unit box [-1,1]
        glm::mat4 localToWorld{1.0f};       //  64  unit box -> world
        glm::vec4 positionRadius{0.0f};     // 128  xyz = capture position (world), w = sphere radius
        glm::vec4 blendNormIntensity{0.0f}; // 144  xyz = per-axis normalized blend band, w = intensity
        glm::vec4 boundsMin{0.0f};          // 160  xyz = world AABB min (cheap reject), w reserved
        glm::vec4 boundsMax{0.0f};          // 176  xyz = world AABB max, w reserved
        // x = cube SLOT. Deliberately explicit rather than implying "probe i uses cube i": the two
        // must be free to diverge the day probes outnumber cube slots and slots become pool-managed.
        // y = GPUProbeShape. z, w reserved.
        glm::uvec4 params{0u};              // 192
        glm::vec4 reserved{0.0f};           // 208  grow here without moving anything above
    };

    static_assert(sizeof(GPUReflectionProbe) == 224, "GPUReflectionProbe must stay 224 B (std430 mirror)");
    static_assert(offsetof(GPUReflectionProbe, worldToLocal) == 0);
    static_assert(offsetof(GPUReflectionProbe, localToWorld) == 64);
    static_assert(offsetof(GPUReflectionProbe, positionRadius) == 128);
    static_assert(offsetof(GPUReflectionProbe, blendNormIntensity) == 144);
    static_assert(offsetof(GPUReflectionProbe, boundsMin) == 160);
    static_assert(offsetof(GPUReflectionProbe, boundsMax) == 176);
    static_assert(offsetof(GPUReflectionProbe, params) == 192);
    static_assert(offsetof(GPUReflectionProbe, reserved) == 208);

    // The SSBO is { uint count; uint pad[3]; GPUReflectionProbe probes[MAX]; } — the same
    // count-header shape FogVolumeBufferManager uses, so a zero header is a valid "no probes" state
    // that needs no separate flag.
    inline constexpr uint32_t PROBE_BUFFER_HEADER_SIZE = 16;
    inline constexpr uint32_t PROBE_BUFFER_SIZE =
        PROBE_BUFFER_HEADER_SIZE + MAX_REFLECTION_PROBES * sizeof(GPUReflectionProbe);
}
