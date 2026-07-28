#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>
#include "../components/TextEffects.hpp"

namespace text
{
    // VK-1635: what a bare [outline] / [shadow] / [glow] tag resolves to when the label it
    // sits on has that effect switched off, so the tag is never a silent no-op. A tag that
    // carries its own number always wins; a tag on a label that already has the effect
    // inherits the label's value. Layout pixels, like every other text-effect distance.
    inline constexpr float RICH_TEXT_DEFAULT_OUTLINE_WIDTH = 1.0f;
    inline constexpr float RICH_TEXT_DEFAULT_SHADOW_OFFSET = 1.0f;
    inline constexpr float RICH_TEXT_DEFAULT_GLOW_RANGE = 2.0f;

    // Per-codepoint style resolved from rich text markup.
    // styleFlags matches the UI text pipeline: bit0 = bold, bit1 = italic.
    struct RichTextSpanStyle
    {
        uint32_t styleFlags = 0;
        bool hasColor = false;
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

        // VK-1635 per-span effect overrides. Each flag is independent: a span may set an
        // outline without disturbing the label's shadow.
        bool hasOutline = false;
        glm::vec4 outlineColor{0.0f, 0.0f, 0.0f, 1.0f};
        float outlineWidth = 0.0f;   // 0 = not given in the tag; resolved by applyTo()

        bool hasShadow = false;
        // Whether the tag NAMED a colour. [shadow] is the one effect tag with a bare
        // form, and a bare one inherits the label's colour; [outline=..]/[glow=..] only
        // exist in the "=" form, which parseEffectTagValue rejects without a colour, so
        // they need no such flag.
        bool hasShadowColor = false;
        glm::vec4 shadowColor{0.0f, 0.0f, 0.0f, 0.5f};
        glm::vec2 shadowOffset{0.0f, 0.0f};  // (0,0) = not given; resolved by applyTo()

        bool hasGlow = false;
        glm::vec4 glowColor{1.0f, 1.0f, 0.6f, 1.0f};
        float glowRange = 0.0f;      // 0 = not given; resolved by applyTo()

        [[nodiscard]] bool overridesEffects() const noexcept
        {
            return hasOutline || hasShadow || hasGlow;
        }

        // Layer this span's overrides on top of the label's own settings. Effects the span
        // does not mention pass through untouched.
        [[nodiscard]] components::TextEffectSettings applyTo(
            const components::TextEffectSettings& base) const noexcept
        {
            components::TextEffectSettings out = base;

            if (hasOutline)
            {
                out.outlineColor = outlineColor;
                out.outlineWidth = outlineWidth > 0.0f
                                       ? outlineWidth
                                       : (base.outlineWidth > 0.0f ? base.outlineWidth
                                                                   : RICH_TEXT_DEFAULT_OUTLINE_WIDTH);
            }
            if (hasShadow)
            {
                // A bare [shadow] inherits the label's colour rather than stamping the
                // parser's placeholder over it. The struct default here is the same
                // (0,0,0,0.5) the bare tag used to hard-code, so a label that never set
                // a shadow colour renders exactly as before.
                if (hasShadowColor)
                {
                    out.shadowColor = shadowColor;
                }
                const bool given = shadowOffset.x != 0.0f || shadowOffset.y != 0.0f;
                if (given)
                {
                    out.shadowOffset = shadowOffset;
                }
                else if (base.shadowOffset.x == 0.0f && base.shadowOffset.y == 0.0f)
                {
                    out.shadowOffset = glm::vec2(RICH_TEXT_DEFAULT_SHADOW_OFFSET);
                }
            }
            if (hasGlow)
            {
                out.glowColor = glowColor;
                out.glowRange = glowRange > 0.0f
                                    ? glowRange
                                    : (base.glowRange > 0.0f ? base.glowRange
                                                             : RICH_TEXT_DEFAULT_GLOW_RANGE);
            }

            return out;
        }
    };

    struct RichTextResult
    {
        std::string strippedText;
        // One entry per decoded codepoint of strippedText, in the same
        // enumeration order layoutText uses (every decodeUTF8 result counts).
        std::vector<RichTextSpanStyle> perCodepoint;
    };

    // Parses a BBCode subset: [b]..[/b], [i]..[/i], [color=#RRGGBB]..[/color]
    // (also #RRGGBBAA), and the VK-1635 effect tags
    //   [outline=#RRGGBB(AA)(,width)]..[/outline]
    //   [shadow]..[/shadow]  or  [shadow=#RRGGBB(AA)(,dx,dy)]..[/shadow]
    //   [glow=#RRGGBB(AA)(,range)]..[/glow]
    // Tags nest; [[ escapes a literal '['. Unknown or malformed tags render
    // literally. [icon=...] is consumed and skipped (reserved for a future
    // inline-icon feature).
    RichTextResult parseRichText(const std::string& markup);
}
