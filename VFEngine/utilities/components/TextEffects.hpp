#pragma once
#include <glm/glm.hpp>

namespace components
{
    // VK-1635: outline / drop shadow / glow for distance-field text.
    //
    // Shared by UILabelComponent (screen-space UI text) and TextComponent (world-space
    // text) so both authoring surfaces, both serializers and both render paths speak the
    // same struct. The GPU consumes a packed form built by text::buildTextEffectInstance().
    //
    // UNITS: every distance is in LAYOUT PIXELS at the label's own fontSize - the same
    // space as letterSpacing. For screen-space UI that is literally screen pixels; for
    // world-space text it scales with the glyph, which is what you want (an outline that
    // stayed a fixed number of screen pixels would fatten as the text receded).
    //
    // ENABLE RULE: an effect is on iff its distance is > 0 AND its colour alpha is > 0.
    // There are no separate enable flags, so there is no state where a configured effect
    // is silently doing nothing because a bool elsewhere is false.
    //
    // Effects need a distance field. On GRAYSCALE_8 and colour-emoji RGBA_32 atlases
    // (spread == edgeValue == 0) they are inert; buildTextEffectInstance() drops them.
    struct TextEffectSettings
    {
        // Distance the outline extends BEYOND the glyph edge.
        float outlineWidth = 0.0f;
        glm::vec4 outlineColor{0.0f, 0.0f, 0.0f, 1.0f};

        // Displacement of the drop shadow. +x is right, +y is down, matching layout space.
        glm::vec2 shadowOffset{0.0f, 0.0f};
        glm::vec4 shadowColor{0.0f, 0.0f, 0.0f, 0.5f};

        // Distance over which the glow falls off outward from the glyph edge.
        float glowRange = 0.0f;
        glm::vec4 glowColor{1.0f, 1.0f, 0.6f, 1.0f};

        [[nodiscard]] bool hasOutline() const noexcept
        {
            return outlineWidth > 0.0f && outlineColor.a > 0.0f;
        }

        [[nodiscard]] bool hasShadow() const noexcept
        {
            return shadowColor.a > 0.0f &&
                   (shadowOffset.x != 0.0f || shadowOffset.y != 0.0f);
        }

        [[nodiscard]] bool hasGlow() const noexcept
        {
            return glowRange > 0.0f && glowColor.a > 0.0f;
        }

        [[nodiscard]] bool any() const noexcept
        {
            return hasOutline() || hasShadow() || hasGlow();
        }

        // Every distance scaled by s. The UI frame builder applies its DPI/layout scale to
        // fontSize; the effects have to travel with it or an outline authored at 2 px would
        // stay 2 px on a 2x display while the glyph it hugs doubled.
        [[nodiscard]] TextEffectSettings scaledBy(float s) const noexcept
        {
            TextEffectSettings out = *this;
            out.outlineWidth *= s;
            out.shadowOffset *= s;
            out.glowRange *= s;
            return out;
        }
    };
}
