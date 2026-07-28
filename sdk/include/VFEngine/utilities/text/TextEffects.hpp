#pragma once
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "../components/TextEffects.hpp"
#include "../math/MathHelper.hpp"

namespace text
{
    // VK-1635: bit positions in TextEffectInstance::colors.w, mirrored by
    // TEXT_EFFECT_* in resources/shaders/common/text_sdf.glsl. Keep the two in step.
    inline constexpr uint32_t TEXT_EFFECT_OUTLINE = 0x1u;
    inline constexpr uint32_t TEXT_EFFECT_SHADOW = 0x2u;
    inline constexpr uint32_t TEXT_EFFECT_GLOW = 0x4u;

    // The GPU-facing form of TextEffectSettings, as it lands in TextCharInstance /
    // UITextCharInstance.
    struct TextEffectInstance
    {
        // (outlineWidth, shadowOffsetX, shadowOffsetY, glowRange), all in ATLAS TEXELS.
        //
        // Texels, not pixels, because that is the unit the fragment shader can convert
        // from for free: the distance field is defined in texels, so texels / pxRange
        // (MTSDF) or texels * edge / spread (legacy SDF) gives field units directly. It
        // also makes the shadow's uv displacement a plain offset / atlasSize with no
        // derivatives, which in turn makes it follow italic shear and world-space
        // billboard rotation without any extra work.
        glm::vec4 params{0.0f};

        // (outlineRGBA8, shadowRGBA8, glowRGBA8, flags). Colours packed by
        // math::packRGBA8 for GLSL's unpackUnorm4x8.
        glm::uvec4 colors{0u};

        // Quad inflation, in LAYOUT PIXELS (the space of TextCharInstance::charSize).
        // Zero when no effect is active, which is what keeps effect-free text on exactly
        // the geometry it has today.
        float marginPx = 0.0f;

        [[nodiscard]] bool any() const noexcept { return colors.w != 0u; }
    };

    // Convert authored settings into instance data.
    //
    // scale            - fontSize / metadata.baseFontSize, i.e. layout pixels per atlas
    //                    texel. Matches the `scale` computed in TextLayout.cpp.
    // hasDistanceField - resource::sdfSmoothWidth(font) > 0. Bitmap and colour-emoji
    //                    atlases bake spread = edgeValue = 0 and carry no distance
    //                    information, so effects are dropped rather than rendered wrong.
    //
    // Header-only on purpose: it is pure arithmetic, and Graphics, Editor and Tests all
    // want it without a DLL export boundary in the way.
    [[nodiscard]] inline TextEffectInstance buildTextEffectInstance(
        const components::TextEffectSettings& settings, float scale, bool hasDistanceField)
    {
        TextEffectInstance out{};
        // Written as !(scale > 0) rather than scale <= 0 because NaN fails EVERY
        // comparison: `scale <= 0.0f` is false for NaN and would let it through, and
        // 1.0f / NaN then poisons every distance below. fontSize reaches here from
        // scripts and from scene JSON, neither of which validates it.
        if (!hasDistanceField || !(scale > 0.0f))
        {
            return out;
        }

        const float pxToTexels = 1.0f / scale;
        uint32_t flags = 0u;

        // Authored pixels -> atlas texels, with the same NaN-safe ordering. The upper
        // bound is far past anything useful (the field itself only represents a couple of
        // texels) but keeps a scripted 1e9 offset from inflating the glyph quad to the
        // point of swallowing the frame in overdraw.
        constexpr float maxReachTexels = 256.0f;
        auto toTexels = [pxToTexels](float px) -> float
        {
            const float t = px * pxToTexels;
            if (!(t > -maxReachTexels)) return (t < 0.0f) ? -maxReachTexels : 0.0f;
            if (!(t < maxReachTexels)) return maxReachTexels;
            return t;
        };

        // How far, in texels, the drawn ink can now reach past the glyph's own atlas cell.
        // Drives the quad inflation below.
        float reachTexels = 0.0f;
        float outlineTexels = 0.0f;

        if (settings.hasOutline())
        {
            outlineTexels = toTexels(settings.outlineWidth);
            out.params.x = outlineTexels;
            out.colors.x = math::packRGBA8(settings.outlineColor.r, settings.outlineColor.g,
                                           settings.outlineColor.b, settings.outlineColor.a);
            flags |= TEXT_EFFECT_OUTLINE;
            reachTexels = (std::max)(reachTexels, outlineTexels);
        }

        if (settings.hasShadow())
        {
            out.params.y = toTexels(settings.shadowOffset.x);
            out.params.z = toTexels(settings.shadowOffset.y);
            out.colors.y = math::packRGBA8(settings.shadowColor.r, settings.shadowColor.g,
                                           settings.shadowColor.b, settings.shadowColor.a);
            flags |= TEXT_EFFECT_SHADOW;

            // The shader casts the shadow of the OUTLINED silhouette, not the bare glyph,
            // so that a thick outline does not overhang its own shadow. Its reach is
            // therefore the displacement plus the outline width. max() of the two axes
            // rather than the length: the margin is one scalar applied to both.
            const float shadowSpan = (std::max)(std::fabs(out.params.y), std::fabs(out.params.z));
            reachTexels = (std::max)(reachTexels, shadowSpan + outlineTexels);
        }

        if (settings.hasGlow())
        {
            out.params.w = toTexels(settings.glowRange);
            out.colors.z = math::packRGBA8(settings.glowColor.r, settings.glowColor.g,
                                           settings.glowColor.b, settings.glowColor.a);
            flags |= TEXT_EFFECT_GLOW;
            reachTexels = (std::max)(reachTexels, out.params.w);
        }

        out.colors.w = flags;
        if (flags != 0u)
        {
            // One extra texel covers the anti-aliasing band at the far edge of the reach.
            // The cell's own baked padding is NOT subtracted: over-inflating only costs a
            // ring of fragments that discard on alpha, whereas under-inflating clips the
            // effect, and the padding differs per atlas format.
            out.marginPx = (reachTexels + 1.0f) * scale;
        }

        return out;
    }
}
