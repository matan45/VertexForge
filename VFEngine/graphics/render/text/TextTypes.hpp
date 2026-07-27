#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include "../common/CameraTypes.hpp"
#include "components/UIComponents.hpp"

namespace render::text
{
    struct TextVertex
    {
        glm::vec2 position;
        glm::vec2 texCoord;

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 0;
            bindingDescription.stride = sizeof(TextVertex);
            bindingDescription.inputRate = vk::VertexInputRate::eVertex;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 2> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 2> attributes{};

            attributes[0].binding = 0;
            attributes[0].location = 0;
            attributes[0].format = vk::Format::eR32G32Sfloat;
            attributes[0].offset = offsetof(TextVertex, position);

            attributes[1].binding = 0;
            attributes[1].location = 1;
            attributes[1].format = vk::Format::eR32G32Sfloat;
            attributes[1].offset = offsetof(TextVertex, texCoord);

            return attributes;
        }
    };

    struct TextCharInstance
    {
        glm::vec3 worldPosition;
        float fontSize;
        glm::vec2 charOffset;
        glm::vec2 charSize;
        glm::vec4 uvRect;       // u0, v0, u1, v1 in atlas
        glm::vec4 color;
        uint32_t renderMode;    // 0 = ScreenSpace, 1 = WorldSpace
        uint32_t entityId;
        float sdfEdge;
        float sdfSmooth;
        uint32_t styleFlags;    // bit0 = bold, bit1 = italic

        // VK-1635 text effects. Built by text::buildTextEffectInstance(); all-zero means
        // no effect and the shader then takes exactly the pre-VK-1635 path.
        glm::vec4 effectParams;   // (outlineWidth, shadowX, shadowY, glowRange) in atlas TEXELS
        glm::uvec4 effectColors;  // (outlineRGBA8, shadowRGBA8, glowRGBA8, flags)
        float effectMargin;       // quad inflation in layout pixels; 0 when no effect

        static vk::VertexInputBindingDescription getBindingDescription()
        {
            vk::VertexInputBindingDescription bindingDescription{};
            bindingDescription.binding = 1;
            bindingDescription.stride = sizeof(TextCharInstance);
            bindingDescription.inputRate = vk::VertexInputRate::eInstance;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 10> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 10> attributes{};

            // location 2: worldPosition (vec3) + fontSize (float) packed as vec4
            attributes[0].binding = 1;
            attributes[0].location = 2;
            attributes[0].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[0].offset = offsetof(TextCharInstance, worldPosition);

            // location 3: charOffset (vec2) + charSize (vec2) packed as vec4
            attributes[1].binding = 1;
            attributes[1].location = 3;
            attributes[1].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[1].offset = offsetof(TextCharInstance, charOffset);

            // location 4: uvRect (vec4)
            attributes[2].binding = 1;
            attributes[2].location = 4;
            attributes[2].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[2].offset = offsetof(TextCharInstance, uvRect);

            // location 5: color (vec4)
            attributes[3].binding = 1;
            attributes[3].location = 5;
            attributes[3].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[3].offset = offsetof(TextCharInstance, color);

            // location 6: renderMode (uint) + entityId (uint) packed as uvec2
            attributes[4].binding = 1;
            attributes[4].location = 6;
            attributes[4].format = vk::Format::eR32G32Uint;
            attributes[4].offset = offsetof(TextCharInstance, renderMode);

            // location 7: sdfEdge (float) + sdfSmooth (float) packed as vec2
            attributes[5].binding = 1;
            attributes[5].location = 7;
            attributes[5].format = vk::Format::eR32G32Sfloat;
            attributes[5].offset = offsetof(TextCharInstance, sdfEdge);

            // location 8: styleFlags (uint)
            attributes[6].binding = 1;
            attributes[6].location = 8;
            attributes[6].format = vk::Format::eR32Uint;
            attributes[6].offset = offsetof(TextCharInstance, styleFlags);

            // location 9: effectParams (vec4)
            attributes[7].binding = 1;
            attributes[7].location = 9;
            attributes[7].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[7].offset = offsetof(TextCharInstance, effectParams);

            // location 10: effectColors (uvec4)
            attributes[8].binding = 1;
            attributes[8].location = 10;
            attributes[8].format = vk::Format::eR32G32B32A32Uint;
            attributes[8].offset = offsetof(TextCharInstance, effectColors);

            // location 11: effectMargin (float)
            attributes[9].binding = 1;
            attributes[9].location = 11;
            attributes[9].format = vk::Format::eR32Sfloat;
            attributes[9].offset = offsetof(TextCharInstance, effectMargin);

            return attributes;
        }
    };

    // glm is packed here (GLM_FORCE_DEFAULT_ALIGNED_GENTYPES is not defined anywhere in
    // this build), so vec3 is 12 bytes with no tail padding and every member sits on its
    // natural 4-byte boundary. Pinned because the vertex-input offsets above are computed
    // from offsetof: a silent layout change would feed the shader the wrong bytes without
    // tripping a single validation error.
    static_assert(sizeof(TextCharInstance) == 120,
                  "TextCharInstance layout is mirrored by text.glsl's instance attributes");
    static_assert(offsetof(TextCharInstance, effectParams) == 84);
    static_assert(offsetof(TextCharInstance, effectColors) == 100);
    static_assert(offsetof(TextCharInstance, effectMargin) == 116);

    using TextCameraUBO = render::common::CameraUBO;

    struct TextPushConstants
    {
        glm::vec2 viewportSize;
        uint32_t glyphMode;  // 0 = field/coverage, 1 = color bitmap, 2 = MTSDF
        // VK-1634: SDFParameters::pxRange for glyphMode 2, zero otherwise. The fragment
        // stage turns it into the screen-space anti-aliasing band. Zero degrades to a
        // one-pixel band rather than misbehaving, so a missed push site is soft, not fatal.
        // Occupies what used to be dead padding, so the block is still 16 bytes and the
        // pipeline layout (TextPipelineSetup.cpp: pushConstantSize = sizeof(...)) is
        // unchanged.
        float pxRange;
    };

    // Mirrors the push_constant block in resources/shaders/text/text.glsl, which declares it
    // identically in both stages. Nothing else cross-checks the two - these fire at compile
    // time; the GLSL side is pinned by tests/test_text_shader_compile.cpp.
    static_assert(sizeof(TextPushConstants) == 16,
                  "text.glsl push_constant block is 16 bytes; keep C++ and GLSL in lockstep");
    static_assert(offsetof(TextPushConstants, viewportSize) == 0);
    static_assert(offsetof(TextPushConstants, glyphMode) == 8);
    static_assert(offsetof(TextPushConstants, pxRange) == 12);

    struct TextRenderData
    {
        std::string fontPath;
        std::string text;
        glm::vec3 worldPosition;
        float fontSize;
        glm::vec4 color;
        uint32_t renderMode;   // 0 = ScreenSpace, 1 = WorldSpace
        uint32_t entityId;
        float lineSpacing;
        float letterSpacing;
        float maxWidth;
        uint8_t horizontalAlignment = 0; // 0=Left, 1=Center, 2=Right
        uint8_t verticalAlignment = 0;   // 0=Top, 1=Middle, 2=Bottom
        float rectHeight = 0.0f;         // Bounding rect height for vertical alignment
        components::FontStyle fontStyle = components::FontStyle::Normal;
        // VK-1635. Distances are layout pixels at this fontSize, so they scale with the
        // glyph as world-space text recedes.
        components::TextEffectSettings effects;
    };
}
