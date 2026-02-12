#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <string>

namespace render::ui
{
    struct UIVertex
    {
        glm::vec2 position;
        glm::vec2 texCoord;

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(UIVertex);
            bindingDescription.inputRate = vk::VertexInputRate::eVertex;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 2> attributes{};

            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = vk::Format::eR32G32Sfloat;
            attributes[0].offset = offsetof(UIVertex, position);

            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = vk::Format::eR32G32Sfloat;
            attributes[1].offset = offsetof(UIVertex, texCoord);

            return attributes;
        }
    };

    struct UIImageInstance
    {
        glm::vec4 posAndSize;  // xy = pixel position, zw = pixel size
        glm::vec4 colorTint;   // RGBA color tint

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 1;
            bindingDescription.stride = sizeof(UIImageInstance);
            bindingDescription.inputRate = vk::VertexInputRate::eInstance;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 2> attributes{};

            attributes[0].binding = 1;
            attributes[0].location = 2;
            attributes[0].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[0].offset = offsetof(UIImageInstance, posAndSize);

            attributes[1].binding = 1;
            attributes[1].location = 3;
            attributes[1].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[1].offset = offsetof(UIImageInstance, colorTint);

            return attributes;
        }
    };

    struct UIPushConstants
    {
        glm::vec2 viewportSize;
        glm::vec2 padding;
    };

    inline constexpr std::array<UIVertex, 4> QUAD_VERTICES = {{
        {{-0.5f, -0.5f}, {0.0f, 0.0f}},
        {{ 0.5f, -0.5f}, {1.0f, 0.0f}},
        {{ 0.5f,  0.5f}, {1.0f, 1.0f}},
        {{-0.5f,  0.5f}, {0.0f, 1.0f}},
    }};

    inline constexpr std::array<uint16_t, 6> QUAD_INDICES = {0, 1, 2, 2, 3, 0};

    struct UIImageRenderData
    {
        std::string texturePath;
        glm::vec2 position;    // pixel position
        glm::vec2 size;        // pixel size
        glm::vec4 colorTint{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec4 scissorRect{0.0f, 0.0f, 0.0f, 0.0f}; // x, y, width, height; 0,0,0,0 = full viewport
    };
}
