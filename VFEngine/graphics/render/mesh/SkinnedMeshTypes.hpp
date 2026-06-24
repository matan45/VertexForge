#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include "resource/Types.hpp"
#include <array>
#include <cmath>
#include <cstddef>

namespace render::mesh
{
    constexpr uint32_t MAX_BONES = 128;

    struct BoneMatricesSSBO
    {
        alignas(16) glm::mat4 boneMatrices[MAX_BONES];
        alignas(4) uint32_t activeBoneCount = 0;
        alignas(4) uint32_t padding[3] = {0, 0, 0}; // Pad to 16-byte alignment
    };

    // VK-1433: per-slot material texture index sentinel. A slot set to NONE means "no
    // texture bound for this slot" — the shader then falls back to the scalar PBR value.
    // The default state (all slots NONE) makes the skinned shader byte-identical to its
    // original scalar-only behavior, so callers that never bind material textures
    // (AnimatedMeshPreviewController) are unaffected.
    inline constexpr uint8_t SKINNED_TEXTURE_INDEX_NONE = 255;

    // The skinned-mesh shader's set 1 holds 16 combined-image-samplers; slot order matches
    // the static mesh shader / MaterialTexturePaths (0=albedo,1=normal,2=ORM,3=metallic,
    // 4=roughness,5=ao,6=emission,7=height).
    inline constexpr uint32_t SKINNED_MATERIAL_TEXTURE_SLOTS = 16;

    // VK-1433 Phase 3 — debug shading modes. The DEFAULT (0) leaves the shader's IBL output
    // byte-identical to its original behavior; the others are prefab-rig-preview-only diagnostics.
    // Must match the `debugMode` branch in skinned_mesh.glsl exactly.
    enum class SkinnedDebugMode : uint32_t
    {
        None = 0,         // unchanged: textured/scalar PBR under IBL
        Clay = 1,         // neutral 0.8 albedo, roughness 1, metallic 0 — form-reading under IBL
        Normals = 2,      // world-space normal visualized as N*0.5+0.5
        UVs = 3,          // texcoord visualized as vec3(uv, 0)
        AlbedoUnlit = 4,  // raw albedo, no lighting
    };

    // VK-1433 Phase 3 — analytic lighting mode. The DEFAULT (0) is IBL-only (today's look);
    // ThreePoint adds an analytic key+fill+rim term on top of the IBL ambient. Must match the
    // `lightingMode` branch in skinned_mesh.glsl exactly.
    enum class SkinnedLightingMode : uint32_t
    {
        IBLOnly = 0,
        ThreePoint = 1,
    };

    // VK-1433 Phase 3 — the editor-facing single "Shading:" selection. Folds the in-shader debug
    // modes AND wireframe (a pipeline-state choice, not a shader branch) into one dropdown index.
    // Mirrors PreviewEnvironmentParams::shadingMode (the value that crosses the service boundary).
    enum class SkinnedShadingSelection : uint8_t
    {
        Lit = 0,          // SkinnedDebugMode::None, fill pipeline (default — unchanged output)
        Clay = 1,
        Normals = 2,
        UVs = 3,
        AlbedoUnlit = 4,
        Wireframe = 5,    // SkinnedDebugMode::None, line pipeline
    };

    struct ResolvedSkinnedShading
    {
        SkinnedDebugMode debugMode = SkinnedDebugMode::None;
        bool wireframe = false;
    };

    // Maps the editor shading selection (0..5, the env-channel `shadingMode`) to the in-shader
    // debug mode + the wireframe-pipeline flag. Out-of-range falls back to the unchanged Lit mode.
    // Pure + CPU-testable.
    inline ResolvedSkinnedShading resolveSkinnedShading(uint8_t selection)
    {
        switch (static_cast<SkinnedShadingSelection>(selection))
        {
            case SkinnedShadingSelection::Clay:        return {SkinnedDebugMode::Clay, false};
            case SkinnedShadingSelection::Normals:     return {SkinnedDebugMode::Normals, false};
            case SkinnedShadingSelection::UVs:         return {SkinnedDebugMode::UVs, false};
            case SkinnedShadingSelection::AlbedoUnlit: return {SkinnedDebugMode::AlbedoUnlit, false};
            case SkinnedShadingSelection::Wireframe:   return {SkinnedDebugMode::None, true};
            case SkinnedShadingSelection::Lit:
            default:                                   return {SkinnedDebugMode::None, false};
        }
    }

    // Builds the world-space key-light direction (pointing FROM the surface TOWARD the light) from
    // spherical editor coordinates: azimuth around +Y, elevation above the XZ plane (both radians).
    // Returns a unit vector. Pure + CPU-testable.
    inline glm::vec3 keyLightDirectionFromSpherical(float azimuthRadians, float elevationRadians)
    {
        const float ce = std::cos(elevationRadians);
        const glm::vec3 dir(ce * std::sin(azimuthRadians),
                            std::sin(elevationRadians),
                            ce * std::cos(azimuthRadians));
        const float len = glm::length(dir);
        return (len > 1e-6f) ? dir / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    struct SkinnedMeshRenderData
    {
        glm::mat4 modelMatrix{1.0f};
        glm::vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;

        // Packed per-slot texture indices, 4 bytes per uint (16 slots). Initialized to
        // all-NONE so the shader samples nothing unless a caller opts in via the pipeline's
        // loadMaterial() (which sets these to the matching slot index, e.g. 0 for albedo).
        std::array<uint32_t, 4> textureIndicesPacked{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};

        // VK-1433 Phase 3 — debug shading + analytic lighting. All default to the no-op state, so a
        // caller that leaves them untouched (AnimatedMeshPreviewController) produces today's output.
        uint32_t debugMode = static_cast<uint32_t>(SkinnedDebugMode::None);
        uint32_t lightingMode = static_cast<uint32_t>(SkinnedLightingMode::IBLOnly);
        // World-space primary (key) light direction (points FROM the surface TOWARD the light).
        glm::vec3 keyLightDirection{0.0f, 1.0f, 0.0f};
        float lightingIntensity = 1.0f;

        // VK-1433 Phase 3 — render with PolygonMode::eLine via the pipeline's lazily-built
        // wireframe variant. Off by default => the fill pipeline (original behavior).
        bool wireframe = false;
    };

    struct SkinnedMeshPushConstants
    {
        // --- head: byte-identical to the original scalar-PBR push constant (offsets 0..112) ---
        glm::mat4 model;                                                                  // 0
        glm::vec4 albedo;                                                                 // 64
        float metallic;                                                                   // 80
        float roughness;                                                                  // 84
        float ao;                                                                         // 88
        float emission;                                                                   // 92
        // Matches the GLSL push-constant tail (set in recordCommandBuffer from
        // SkinnedMeshRenderData::textureIndicesPacked; default all-NONE => no sampling).
        std::array<uint32_t, 4> textureIndicesPacked{                                     // 96
            0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};

        // --- VK-1433 Phase 3 tail (offsets 112..144). std430 byte parity with both GLSL stages. ---
        uint32_t debugMode = 0;            // 112  SkinnedDebugMode (0 => unchanged)
        uint32_t lightingMode = 0;         // 116  SkinnedLightingMode (0 => IBL-only, today's look)
        float _pad0 = 0.0f;                // 120  pad so keyLightDirIntensity lands 16-aligned
        float _pad1 = 0.0f;                // 124
        // xyz = world-space key light direction (toward light), w = lightingIntensity.
        glm::vec4 keyLightDirIntensity{0.0f, 1.0f, 0.0f, 1.0f};                           // 128
    };

    // The original scalar-PBR push constant was 112 bytes; the Phase-3 tail adds 32 (-> 144).
    // 144 < the desktop maxPushConstantsSize (≥256 on every desktop GPU; the spec's 128-byte
    // floor is for constrained mobile parts, not this editor's target). The fields after offset
    // 112 default to the no-op state, so the head is unchanged for default callers.
    static_assert(sizeof(SkinnedMeshPushConstants) == 144,
                  "SkinnedMeshPushConstants must stay byte-identical to the GLSL push-constant block");
    static_assert(offsetof(SkinnedMeshPushConstants, textureIndicesPacked) == 96,
                  "textureIndicesPacked must stay at std430 offset 96");
    static_assert(offsetof(SkinnedMeshPushConstants, debugMode) == 112,
                  "debugMode must stay at std430 offset 112");
    static_assert(offsetof(SkinnedMeshPushConstants, keyLightDirIntensity) == 128,
                  "keyLightDirIntensity must stay at std430 offset 128");

    struct SkinnedMeshVertexInput
    {
        static vk::VertexInputBindingDescription getBindingDescription()
        {
            static_assert(sizeof(resource::Vertex) == 64,
                          "Vertex struct size mismatch! Expected 64 bytes for GPU compatibility.");

            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(resource::Vertex);
            bindingDescription.inputRate = vk::VertexInputRate::eVertex;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 5> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 5> attributes{};

            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = vk::Format::eR32G32B32Sfloat;
            attributes[0].offset = 0;

            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = vk::Format::eR32G32B32Sfloat;
            attributes[1].offset = 12;

            attributes[2].binding = 0;
            attributes[2].location = 2;
            attributes[2].format = vk::Format::eR32G32Sfloat;
            attributes[2].offset = 24;

            attributes[3].binding = 0;
            attributes[3].location = 3;
            attributes[3].format = vk::Format::eR32G32B32A32Sint;
            attributes[3].offset = 32;

            attributes[4].binding = 0;
            attributes[4].location = 4;
            attributes[4].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[4].offset = 48;

            return attributes;
        }
    };

    struct AnimationPlaybackState
    {
        float currentTime = 0.0f;
        float duration = 0.0f;
        float playbackSpeed = 1.0f;
        bool isPlaying = false;
        bool looping = true;

        void update(float deltaTime)
        {
            if (!isPlaying) return;

            currentTime += deltaTime * playbackSpeed;

            if (looping && duration > 0.0f)
            {
                while (currentTime >= duration)
                    currentTime -= duration;
                while (currentTime < 0.0f)
                    currentTime += duration;
            }
            else
            {
                currentTime = glm::clamp(currentTime, 0.0f, duration);
                if (currentTime >= duration)
                    isPlaying = false;
            }
        }

        void setTime(float time)
        {
            if (duration > 0.0f && looping)
            {
                currentTime = glm::mod(time, duration);
                if (currentTime < 0.0f)
                    currentTime += duration;
            }
            else
            {
                currentTime = glm::clamp(time, 0.0f, duration);
            }
        }

        void play() { isPlaying = true; }
        void pause() { isPlaying = false; }

        void stop()
        {
            isPlaying = false;
            currentTime = 0.0f;
        }
    };
}
