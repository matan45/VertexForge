#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include "resource/Types.hpp"
#include <array>
#include <string>
#include <vector>

namespace render::mesh
{
    // Maximum number of bones supported per skeleton
    constexpr uint32_t MAX_BONES = 128;

    // Bone matrices uniform buffer - passed as SSBO for larger bone counts
    struct BoneMatricesSSBO
    {
        alignas(16) glm::mat4 boneMatrices[MAX_BONES];
        alignas(4) uint32_t activeBoneCount = 0;
        alignas(4) uint32_t padding[3] = {0, 0, 0}; // Pad to 16-byte alignment
    };

    // Per-instance skinning data for animation preview
    struct SkinnedMeshRenderData
    {
        std::string meshPath;           // Path to .vfMesh file
        std::string animationPath;      // Path to .vfAnim file
        glm::mat4 modelMatrix{1.0f};    // World transform

        // PBR material properties (same as MeshRenderData)
        glm::vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;

        // Current animation state
        float animationTime = 0.0f;     // Current playback time
        bool isPlaying = false;
        bool looping = true;

        // Computed bone matrices (updated each frame by AnimationEvaluator)
        std::vector<glm::mat4> boneMatrices;
    };

    // Push constants for skinned mesh rendering
    struct SkinnedMeshPushConstants
    {
        glm::mat4 model;    // 64 bytes
        glm::vec4 albedo;   // 16 bytes (RGB + alpha)
        float metallic;     // 4 bytes
        float roughness;    // 4 bytes
        float ao;           // 4 bytes
        float emission;     // 4 bytes
        // Total: 96 bytes (within 128-byte push constant limit)
    };

    // Vertex input for skinned meshes (same layout as regular mesh but with bone data active)
    struct SkinnedMeshVertexInput
    {
        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = 64; // sizeof(Vertex): vec3 + vec3 + vec2 + ivec4 + vec4
            bindingDescription.inputRate = vk::VertexInputRate::eVertex;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 5> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 5> attributes{};

            // location 0: position (vec3)
            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = vk::Format::eR32G32B32Sfloat;
            attributes[0].offset = 0;

            // location 1: normal (vec3)
            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = vk::Format::eR32G32B32Sfloat;
            attributes[1].offset = 12;

            // location 2: texCoords (vec2)
            attributes[2].binding = 0;
            attributes[2].location = 2;
            attributes[2].format = vk::Format::eR32G32Sfloat;
            attributes[2].offset = 24;

            // location 3: boneIndices (ivec4)
            attributes[3].binding = 0;
            attributes[3].location = 3;
            attributes[3].format = vk::Format::eR32G32B32A32Sint;
            attributes[3].offset = 32;

            // location 4: boneWeights (vec4)
            attributes[4].binding = 0;
            attributes[4].location = 4;
            attributes[4].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[4].offset = 48;

            return attributes;
        }
    };

    // Animation playback state
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
        void stop() { isPlaying = false; currentTime = 0.0f; }
        void togglePlayPause() { isPlaying = !isPlaying; }
    };
}
