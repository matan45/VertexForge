#pragma once

#include "UIRenderTypes.hpp"
#include "components/UIComponents.hpp"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstddef>
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
            bindingDescription.stride = sizeof(UITextCharInstance);
            bindingDescription.inputRate = vk::VertexInputRate::eInstance;
            return bindingDescription;
        }

        static std::array<vk::VertexInputAttributeDescription, 8> getAttributeDescriptions()
        {
            std::array<vk::VertexInputAttributeDescription, 8> attributes{};

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

            // location 6: styleFlags (uint)
            attributes[4].binding = 1;
            attributes[4].location = 6;
            attributes[4].format = vk::Format::eR32Uint;
            attributes[4].offset = offsetof(UITextCharInstance, styleFlags);

            // location 7: effectParams (vec4)
            attributes[5].binding = 1;
            attributes[5].location = 7;
            attributes[5].format = vk::Format::eR32G32B32A32Sfloat;
            attributes[5].offset = offsetof(UITextCharInstance, effectParams);

            // location 8: effectColors (uvec4)
            attributes[6].binding = 1;
            attributes[6].location = 8;
            attributes[6].format = vk::Format::eR32G32B32A32Uint;
            attributes[6].offset = offsetof(UITextCharInstance, effectColors);

            // location 9: effectMargin (float)
            attributes[7].binding = 1;
            attributes[7].location = 9;
            attributes[7].format = vk::Format::eR32Sfloat;
            attributes[7].offset = offsetof(UITextCharInstance, effectMargin);

            return attributes;
        }
    };

    // See the matching note on TextCharInstance: these offsets feed the vertex-input
    // descriptions via offsetof, and a layout change would mis-feed the shader silently.
    static_assert(sizeof(UITextCharInstance) == 96,
                  "UITextCharInstance layout is mirrored by ui_text.glsl's instance attributes");
    static_assert(offsetof(UITextCharInstance, effectParams) == 60);
    static_assert(offsetof(UITextCharInstance, effectColors) == 76);
    static_assert(offsetof(UITextCharInstance, effectMargin) == 92);

    struct UITextPushConstants
    {
        glm::vec2 viewportSize;
        uint32_t glyphMode;    // 0 = field/coverage, 1 = color bitmap, 2 = MTSDF
        // VK-1634: SDFParameters::pxRange for glyphMode 2, zero otherwise. The fragment
        // stage turns it into the screen-space anti-aliasing band. Zero degrades to a
        // one-pixel band rather than misbehaving, so a missed push site is soft, not fatal.
        // Occupies what used to be dead padding, so the block is still 16 bytes and the
        // pipeline layout (UITextPipelineSetup.cpp: pushConstantSize = sizeof(...)) is
        // unchanged - including the stencil pipeline, which shares that layout.
        float pxRange;
    };

    // Mirrors the push_constant block in resources/shaders/ui/ui_text.glsl, which declares it
    // identically in both stages. Nothing else cross-checks the two - these fire at compile
    // time; the GLSL side is pinned by tests/test_text_shader_compile.cpp.
    static_assert(sizeof(UITextPushConstants) == 16,
                  "ui_text.glsl push_constant block is 16 bytes; keep C++ and GLSL in lockstep");
    static_assert(offsetof(UITextPushConstants, viewportSize) == 0);
    static_assert(offsetof(UITextPushConstants, glyphMode) == 8);
    static_assert(offsetof(UITextPushConstants, pxRange) == 12);

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
        components::FontStyle fontStyle = components::FontStyle::Normal;
        bool wordWrap = true;
        bool richText = false;  // parse BBCode-style markup (UILabel only)
        // VK-1635. Already multiplied by the UI layout scale by UIFrameBuilder, exactly
        // like fontSize, so an outline keeps its proportion to the glyph on a 2x display.
        components::TextEffectSettings effects;
        glm::vec4 scissorRect{0.0f};     // x, y, width, height (0,0,0,0 = full viewport)

        // Stencil masking
        UIStencilOp stencilOp = UIStencilOp::None;
        uint8_t stencilRef = 0;

        // Overlay layer: records after all main UI images and text
        // (tooltips, modal windows). See UIImageRenderData::overlay.
        bool overlay = false;
    };
}
