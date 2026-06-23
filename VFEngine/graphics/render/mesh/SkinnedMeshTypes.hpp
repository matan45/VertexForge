#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include "resource/Types.hpp"
#include <array>

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
    };

    struct SkinnedMeshPushConstants
    {
        glm::mat4 model;
        glm::vec4 albedo;
        float metallic;
        float roughness;
        float ao;
        float emission;
        // Matches the GLSL push-constant tail (set in recordCommandBuffer from
        // SkinnedMeshRenderData::textureIndicesPacked; default all-NONE => no sampling).
        std::array<uint32_t, 4> textureIndicesPacked{0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    };

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
