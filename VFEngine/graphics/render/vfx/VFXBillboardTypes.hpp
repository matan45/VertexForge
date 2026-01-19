#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <string>

namespace render::vfx
{
    // CPU-side particle representation for simulation
    struct VFXParticle
    {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec4 color{1.0f};
        float size = 1.0f;
        float lifetime = 0.0f;      // Current age in seconds
        float maxLifetime = 1.0f;   // Total lifetime in seconds
        bool active = false;
    };

    // GPU-side instance data for billboard rendering
    struct VFXInstanceData
    {
        glm::vec3 worldPosition;
        float size;
        glm::vec4 color;
        float lifetimeRatio;  // 0.0 = just spawned, 1.0 = about to die
        float padding[3];     // Align to 16 bytes

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 1;
            bindingDescription.stride = sizeof(VFXInstanceData);
            bindingDescription.inputRate = vk::VertexInputRate::eInstance;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 4> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 4> attributes{};

            // location 2: worldPosition (vec3) + size (float) packed as vec4
            attributes[0].binding = 1;
            attributes[0].location = 2;
            attributes[0].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[0].offset = offsetof(VFXInstanceData, worldPosition);

            // location 3: color (vec4)
            attributes[1].binding = 1;
            attributes[1].location = 3;
            attributes[1].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[1].offset = offsetof(VFXInstanceData, color);

            // location 4: lifetimeRatio (float)
            attributes[2].binding = 1;
            attributes[2].location = 4;
            attributes[2].format = vk::Format::eR32Sfloat;
            attributes[2].offset = offsetof(VFXInstanceData, lifetimeRatio);

            // location 5: padding (for alignment, unused in shader but keeps struct consistent)
            attributes[3].binding = 1;
            attributes[3].location = 5;
            attributes[3].format = vk::Format::eR32G32B32Sfloat;
            attributes[3].offset = offsetof(VFXInstanceData, padding);

            return attributes;
        }
    };

    // Quad vertex for billboard rendering
    struct VFXQuadVertex
    {
        glm::vec2 position;   // Quad corner offset (-0.5 to 0.5)
        glm::vec2 texCoord;

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(VFXQuadVertex);
            bindingDescription.inputRate = vk::VertexInputRate::eVertex;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 2> attributes{};

            // location 0: position (vec2)
            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = vk::Format::eR32G32Sfloat;
            attributes[0].offset = offsetof(VFXQuadVertex, position);

            // location 1: texCoord (vec2)
            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = vk::Format::eR32G32Sfloat;
            attributes[1].offset = offsetof(VFXQuadVertex, texCoord);

            return attributes;
        }
    };

    // Camera uniform buffer for billboard rendering
    struct VFXCameraUBO
    {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;
        alignas(16) glm::vec3 cameraPos;
        alignas(4) float time;
    };

    // Push constants for per-draw data
    struct VFXPushConstants
    {
        glm::vec2 viewportSize;
        float padding[2];
    };

    // Emitter configuration extracted from VFX graph
    struct VFXEmitterConfig
    {
        float spawnRate = 10.0f;        // Particles per second
        float lifetime = 2.0f;          // Particle lifetime in seconds
        float startSize = 1.0f;         // Initial particle size (scale)
        float startSpeed = 1.0f;        // Initial velocity magnitude (units per second)
        glm::vec4 startColor{1.0f, 1.0f, 1.0f, 1.0f};  // Initial RGBA color
        glm::vec3 emitDirection{0.0f, 1.0f, 0.0f};     // Direction for initial velocity
        std::string texturePath;        // Optional texture path (empty = default white)
    };

    // Constants
    namespace VFXConstants
    {
        inline constexpr size_t MAX_PARTICLES = 1000;
        inline constexpr uint32_t QUAD_VERTEX_COUNT = 4;
        inline constexpr uint32_t QUAD_INDEX_COUNT = 6;
    }
}
