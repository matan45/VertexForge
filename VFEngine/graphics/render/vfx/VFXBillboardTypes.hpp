#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <string>
#include "vfx/VFXModifierTypes.hpp"
#include "vfx/VFXForceTypes.hpp"
#include "vfx/VFXShapeTypes.hpp"

namespace render::vfx
{
    struct VFXParticle
    {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec4 color{1.0f};
        float size = 1.0f;
        float rotation = 0.0f;      // Rotation angle in radians (VK-238)
        float lifetime = 0.0f;
        float maxLifetime = 1.0f;
        bool active = false;

        // Store initial values for modifier calculations (VK-238)
        glm::vec4 initialColor{1.0f};
        float initialSize = 1.0f;
        float initialSpeed = 1.0f;
        glm::vec3 initialDirection{0.0f, 1.0f, 0.0f};  // Stored at spawn for speed modifier

        uint32_t spawnSeed = 0;  // Deterministic seed for flipbook random start (VK-493)
    };

    struct VFXInstanceData
    {
        glm::vec3 worldPosition;
        float size;
        glm::vec4 color;
        float lifetimeRatio;
        float rotation;             // Rotation angle in radians (VK-238)
        float flipbookFrameIndex;   // Computed frame index for flipbook animation (VK-493)
        float padding1;

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 1;
            bindingDescription.stride = sizeof(VFXInstanceData);
            bindingDescription.inputRate = vk::VertexInputRate::eInstance;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 5> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 5> attributes{};

            attributes[0].binding = 1;
            attributes[0].location = 2;
            attributes[0].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[0].offset = offsetof(VFXInstanceData, worldPosition);

            attributes[1].binding = 1;
            attributes[1].location = 3;
            attributes[1].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[1].offset = offsetof(VFXInstanceData, color);

            attributes[2].binding = 1;
            attributes[2].location = 4;
            attributes[2].format = vk::Format::eR32Sfloat;
            attributes[2].offset = offsetof(VFXInstanceData, lifetimeRatio);

            attributes[3].binding = 1;
            attributes[3].location = 5;
            attributes[3].format = vk::Format::eR32Sfloat;
            attributes[3].offset = offsetof(VFXInstanceData, rotation);

            attributes[4].binding = 1;
            attributes[4].location = 6;
            attributes[4].format = vk::Format::eR32Sfloat;
            attributes[4].offset = offsetof(VFXInstanceData, flipbookFrameIndex);

            return attributes;
        }
    };

    struct VFXQuadVertex
    {
        glm::vec2 position;
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

            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = vk::Format::eR32G32Sfloat;
            attributes[0].offset = offsetof(VFXQuadVertex, position);

            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = vk::Format::eR32G32Sfloat;
            attributes[1].offset = offsetof(VFXQuadVertex, texCoord);

            return attributes;
        }
    };

    struct VFXCameraUBO
    {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;
        alignas(16) glm::vec3 cameraPos;
        alignas(4) float time;
    };

    struct VFXEmitterConfig
    {
        float spawnRate = 10.0f;
        float lifetime = 2.0f;
        float startSize = 1.0f;
        float startSpeed = 1.0f;
        glm::vec4 startColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec3 emitDirection{0.0f, 1.0f, 0.0f};
        std::string texturePath;
        bool looping = true;

        // Modifier chain (VK-238)
        ::vfx::VFXModifierChain modifiers;

        // Force chain (VK-239)
        ::vfx::VFXForceChain forces;

        // Shape config (VK-240)
        ::vfx::ShapeConfig shape;

        // Flipbook / Texture Sheet Animation (VK-493)
        int flipbookRows = 1;
        int flipbookColumns = 1;
        float flipbookFrameRate = 0.0f;   // 0 = lifetime-based, >0 = fixed FPS
        bool flipbookRandomStart = false;

        // Rendering
        float alphaClipThreshold = 0.1f;
        bool additiveBlend = false;
    };

    namespace VFXConstants
    {
        inline constexpr size_t MAX_PARTICLES = 1000;
        inline constexpr uint32_t QUAD_INDEX_COUNT = 6;
    }
}
