#pragma once

#include "UIRenderTypes.hpp"
#include "components/UIComponents.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>
#include <string>

namespace render::ui
{
    struct UITextCharInstance
    {
        glm::vec4 posAndSize;   // xy = pixel position of char, zw = char size in pixels
        glm::vec4 uvRect;       // u0, v0, u1, v1 in font atlas
        glm::vec4 color;        // RGBA
        glm::vec2 sdfParams;    // x = sdfEdge, y = sdfSmooth

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 1;
            bindingDescription.stride = sizeof(UITextCharInstance);
            bindingDescription.inputRate = vk::VertexInputRate::eInstance;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 4> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 4> attributes{};

            // location 2: posAndSize (vec4)
            attributes[0].binding = 1;
            attributes[0].location = 2;
            attributes[0].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[0].offset = offsetof(UITextCharInstance, posAndSize);

            // location 3: uvRect (vec4)
            attributes[1].binding = 1;
            attributes[1].location = 3;
            attributes[1].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[1].offset = offsetof(UITextCharInstance, uvRect);

            // location 4: color (vec4)
            attributes[2].binding = 1;
            attributes[2].location = 4;
            attributes[2].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[2].offset = offsetof(UITextCharInstance, color);

            // location 5: sdfParams (vec2)
            attributes[3].binding = 1;
            attributes[3].location = 5;
            attributes[3].format = vk::Format::eR32G32Sfloat;
            attributes[3].offset = offsetof(UITextCharInstance, sdfParams);

            return attributes;
        }
    };

    struct UITextPushConstants
    {
        glm::vec2 viewportSize;
        uint32_t glyphMode;    // 0 = SDF, 1 = color bitmap
        float padding;
    };

    struct UITextRenderData
    {
        std::string fontPath;
        std::string text;
        float fontSize = 16.0f;
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
        float lineSpacing = 1.0f;
        float letterSpacing = 0.0f;
        glm::vec2 position{0.0f};        // pixel pos of label's top-left corner
        glm::vec2 size{0.0f};            // pixel size of label rect (for maxWidth + alignment)
        uint8_t horizontalAlignment = 0;  // 0=Left, 1=Center, 2=Right
        uint8_t verticalAlignment = 0;    // 0=Top, 1=Middle, 2=Bottom
        components::TextOverflow overflow = components::TextOverflow::Overflow;
        bool wordWrap = true;
        glm::vec4 scissorRect{0.0f};     // x, y, width, height (0,0,0,0 = full viewport)

        // Stencil masking
        UIStencilOp stencilOp = UIStencilOp::None;
        uint8_t stencilRef = 0;
    };
}
