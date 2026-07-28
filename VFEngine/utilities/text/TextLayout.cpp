#include "TextLayout.hpp"
#include "../resource/Types.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace text
{
    uint32_t decodeUTF8(const std::string& text, size_t& index)
    {
        if (index >= text.size())
        {
            return 0;
        }

        unsigned char c = static_cast<unsigned char>(text[index]);

        auto isValidContinuation = [&text](size_t idx) -> bool
        {
            if (idx >= text.size()) return false;
            unsigned char b = static_cast<unsigned char>(text[idx]);
            return (b & 0xC0) == 0x80;
        };

        // ASCII (0xxxxxxx)
        if ((c & 0x80) == 0)
        {
            index++;
            return c;
        }

        // 2-byte sequence (110xxxxx 10xxxxxx)
        if ((c & 0xE0) == 0xC0)
        {
            if (!isValidContinuation(index + 1))
            {
                index++;
                return 0xFFFD;
            }
            uint32_t codepoint = (c & 0x1F) << 6;
            codepoint |= (static_cast<unsigned char>(text[index + 1]) & 0x3F);
            index += 2;
            if (codepoint < 0x80)
            {
                return 0xFFFD;
            }
            return codepoint;
        }

        // 3-byte sequence (1110xxxx 10xxxxxx 10xxxxxx)
        if ((c & 0xF0) == 0xE0)
        {
            if (!isValidContinuation(index + 1) || !isValidContinuation(index + 2))
            {
                index++;
                return 0xFFFD;
            }
            uint32_t codepoint = (c & 0x0F) << 12;
            codepoint |= (static_cast<unsigned char>(text[index + 1]) & 0x3F) << 6;
            codepoint |= (static_cast<unsigned char>(text[index + 2]) & 0x3F);
            index += 3;
            if (codepoint < 0x800 || (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            {
                return 0xFFFD;
            }
            return codepoint;
        }

        // 4-byte sequence (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
        if ((c & 0xF8) == 0xF0)
        {
            if (!isValidContinuation(index + 1) || !isValidContinuation(index + 2) ||
                !isValidContinuation(index + 3))
            {
                index++;
                return 0xFFFD;
            }
            uint32_t codepoint = (c & 0x07) << 18;
            codepoint |= (static_cast<unsigned char>(text[index + 1]) & 0x3F) << 12;
            codepoint |= (static_cast<unsigned char>(text[index + 2]) & 0x3F) << 6;
            codepoint |= (static_cast<unsigned char>(text[index + 3]) & 0x3F);
            index += 4;
            if (codepoint < 0x10000 || codepoint > 0x10FFFF)
            {
                return 0xFFFD;
            }
            return codepoint;
        }

        index++;
        return 0xFFFD;
    }

    namespace
    {
        // VK-1636. A FaceSet slot is only usable if it could actually produce
        // glyphs; the single-font entry point's guard is exactly this test on
        // face 0, so reusing it keeps the two paths agreeing about empty output.
        bool faceUsable(const resource::FontData* face)
        {
            return face != nullptr && face->metadata.baseFontSize != 0 &&
                   face->atlas.width != 0 && face->atlas.height != 0;
        }

        // Everything layoutTextStyled needs per slot, hoisted out of the glyph loop
        // the same way the single-font version hoisted `scale` and `lineHeight`.
        struct FaceContext
        {
            const resource::FontData* face = nullptr;
            float scale = 0.0f;
            float atlasW = 0.0f;
            float atlasH = 0.0f;
            // blockAscenderPx - (ascender * scale). Exactly 0.0f for whichever face
            // defines the block baseline, which is why the single-face path stays
            // bit-identical: `y + 0.0f` is exact in IEEE754.
            float baselineShift = 0.0f;
        };

        // Populated slots only; index i of the returned array mirrors faceSet.faces[i].
        // Also yields the block-wide line height and ascender, both a max over the
        // populated slots so every face shares one baseline and one line pitch.
        struct BlockContext
        {
            FaceContext faces[4];
            // Fallbacks use the style block's baseline but never participate in
            // its ascender or line-height maxima.
            FaceContext fallback[MAX_FALLBACK_FACES];
            float lineHeight = 0.0f;
            float ascenderPx = 0.0f;
        };

        BlockContext buildBlockContext(const FaceSet& faceSet, float fontSize, float lineSpacing)
        {
            BlockContext block;

            bool any = false;
            for (int i = 0; i < 4; ++i)
            {
                const resource::FontData* face = faceSet.faces[i];
                if (!faceUsable(face)) continue;

                FaceContext& ctx = block.faces[i];
                ctx.face = face;
                ctx.scale = fontSize / static_cast<float>(face->metadata.baseFontSize);
                ctx.atlasW = static_cast<float>(face->atlas.width);
                ctx.atlasH = static_cast<float>(face->atlas.height);

                // Spelled exactly as the single-font path spells it, so a one-face
                // set reproduces the legacy value bit for bit.
                const float faceLineHeight = face->metadata.lineHeight * ctx.scale * lineSpacing;
                const float faceAscenderPx = face->metadata.ascender * ctx.scale;

                if (!any)
                {
                    block.lineHeight = faceLineHeight;
                    block.ascenderPx = faceAscenderPx;
                    any = true;
                }
                else
                {
                    block.lineHeight = std::max(block.lineHeight, faceLineHeight);
                    block.ascenderPx = std::max(block.ascenderPx, faceAscenderPx);
                }
            }

            for (int i = 0; i < 4; ++i)
            {
                FaceContext& ctx = block.faces[i];
                if (!ctx.face) continue;
                ctx.baselineShift =
                    block.ascenderPx - ctx.face->metadata.ascender * ctx.scale;
            }

            for (uint8_t i = 0; i < MAX_FALLBACK_FACES; ++i)
            {
                const resource::FontData* face = faceSet.fallback[i];
                if (!faceUsable(face)) continue;

                FaceContext& ctx = block.fallback[i];
                ctx.face = face;
                ctx.scale = fontSize / static_cast<float>(face->metadata.baseFontSize);
                ctx.atlasW = static_cast<float>(face->atlas.width);
                ctx.atlasH = static_cast<float>(face->atlas.height);
                ctx.baselineShift =
                    block.ascenderPx - face->metadata.ascender * ctx.scale;
            }

            return block;
        }

        [[nodiscard]] const FaceContext* contextFor(
            const BlockContext& block,
            uint8_t faceIndex) noexcept
        {
            if (faceIndex < 4)
            {
                return &block.faces[faceIndex];
            }

            const uint8_t fallbackIndex = static_cast<uint8_t>(faceIndex - 4);
            if (fallbackIndex < MAX_FALLBACK_FACES)
            {
                return &block.fallback[fallbackIndex];
            }

            return nullptr;
        }
    }

    LayoutResult layoutText(
        const resource::FontData& fontData,
        const std::string& text,
        float fontSize,
        float maxWidth,
        float lineSpacing,
        float letterSpacing)
    {
        FaceSet faceSet;
        faceSet.faces[0] = &fontData;
        return layoutTextStyled(faceSet, text, {}, fontSize, maxWidth, lineSpacing, letterSpacing);
    }

    LayoutResult layoutTextStyled(
        const FaceSet& faceSet,
        const std::string& text,
        std::span<const uint8_t> perCodepointFace,
        float fontSize,
        float maxWidth,
        float lineSpacing,
        float letterSpacing)
    {
        LayoutResult result;

        if (text.empty() || !faceUsable(faceSet.faces[0]))
        {
            return result;
        }

        const BlockContext block = buildBlockContext(faceSet, fontSize, lineSpacing);
        const float lineHeight = block.lineHeight;

        float cursorX = 0.0f;
        float cursorY = 0.0f;
        float maxX = 0.0f;

        uint32_t prevCodepoint = 0;
        uint8_t prevFaceIndex = 0;
        size_t i = 0;
        uint32_t charCounter = 0;

        // Track word boundaries for word wrap
        size_t wordStartGlyphIndex = 0;
        float wordStartCursorX = 0.0f;

        while (i < text.size())
        {
            uint32_t codepoint = decodeUTF8(text, i);
            uint32_t charIndex = charCounter++;

            if (codepoint == '\n')
            {
                if (cursorX > maxX) maxX = cursorX;
                cursorX = 0.0f;
                cursorY += lineHeight;
                prevCodepoint = 0;
                wordStartGlyphIndex = result.glyphs.size();
                wordStartCursorX = 0.0f;
                continue;
            }

            if (codepoint == '\r')
            {
                continue;
            }

            // Track word start for wrapping
            if (codepoint == ' ')
            {
                wordStartGlyphIndex = result.glyphs.size() + 1;
                wordStartCursorX = cursorX;
            }

            // VK-1636: which face draws this codepoint. Out-of-range and unpopulated
            // slots collapse to 0, so an empty perCodepointFace is plain single-font
            // layout.
            const uint8_t wantFace =
                (charIndex < perCodepointFace.size()) ? perCodepointFace[charIndex] : uint8_t{0};
            // Resolve against the BLOCK, not the FaceSet: a slot can be non-null yet
            // unusable, and block.faces[i].face is null in that case.
            uint8_t faceIndex =
                (wantFace < 4 && block.faces[wantFace].face != nullptr) ? wantFace : uint8_t{0};

            const resource::GlyphData* glyph = block.faces[faceIndex].face->findGlyph(codepoint);
            if (!glyph && faceIndex != 0)
            {
                // A styled face imported with narrower character ranges would leave
                // holes in otherwise-fine text. Borrow the glyph from the base face
                // instead - it renders unstyled, which beats rendering nothing.
                glyph = block.faces[0].face->findGlyph(codepoint);
                if (glyph) faceIndex = 0;
            }

            if (!glyph)
            {
                for (uint8_t fallbackIndex = 0;
                     fallbackIndex < MAX_FALLBACK_FACES;
                     ++fallbackIndex)
                {
                    const FaceContext& fallbackCtx = block.fallback[fallbackIndex];
                    if (!fallbackCtx.face) continue;

                    glyph = fallbackCtx.face->findGlyph(codepoint);
                    if (glyph)
                    {
                        faceIndex = static_cast<uint8_t>(4 + fallbackIndex);
                        break;
                    }
                }
            }

            bool emitTofu = false;
            if (!glyph)
            {
                const MissingClass classification = missingClass(codepoint);
                if (classification == MissingClass::ZeroWidth)
                {
                    prevCodepoint = codepoint;
                    prevFaceIndex = faceIndex;
                    continue;
                }
                if (classification == MissingClass::SpaceLike)
                {
                    cursorX += fontSize * 0.5f;
                    prevCodepoint = codepoint;
                    prevFaceIndex = faceIndex;
                    continue;
                }

                emitTofu = true;
                faceIndex = TOFU_FACE_INDEX;
            }

            const FaceContext* faceCtx = contextFor(block, faceIndex);
            if (!emitTofu && (faceCtx == nullptr || faceCtx->face == nullptr))
            {
                // Resolution above only selects usable contexts. Keep this total
                // in case a future face-index producer violates that contract.
                cursorX += fontSize * 0.5f;
                prevCodepoint = codepoint;
                prevFaceIndex = faceIndex;
                continue;
            }

            const float scale = emitTofu ? 0.0f : faceCtx->scale;

            // Kerning pairs are per face; a pair that spans a [b] boundary describes
            // two glyphs that were never designed together, so drop it. Tofu has
            // no face and therefore never participates in a pair.
            if (!emitTofu && prevCodepoint != 0 && prevFaceIndex == faceIndex)
            {
                cursorX += faceCtx->face->getKerning(prevCodepoint, codepoint) * scale;
            }

            const float glyphAdvance = emitTofu
                ? fontSize * TOFU_ADVANCE_EM + letterSpacing
                : glyph->advanceX * scale + letterSpacing;

            // Word wrap check
            if (maxWidth > 0.0f && cursorX + glyphAdvance > maxWidth && cursorX > 0.0f)
            {
                // Wrap at word boundary if possible
                if (wordStartGlyphIndex > 0 && wordStartGlyphIndex <= result.glyphs.size() &&
                    codepoint != ' ')
                {
                    float wrapOffset = wordStartCursorX;
                    for (size_t g = wordStartGlyphIndex; g < result.glyphs.size(); ++g)
                    {
                        result.glyphs[g].offset.x -= wrapOffset;
                        result.glyphs[g].offset.y += lineHeight;
                        // VK-1632: the moved glyphs now sit on the next line, so their
                        // line origin has to move with them. Without this, partitionLines
                        // (and therefore applyEllipsis and alignment) groups the wrapped
                        // word with the line it was pushed off.
                        result.glyphs[g].lineY += lineHeight;
                    }
                    // VK-1632: wrapOffset IS the completed line's width (cursorX at the
                    // word boundary). The old test compared the *new* line's width so far
                    // and could both miss a wider completed line and clobber maxX downward.
                    if (wrapOffset > maxX) maxX = wrapOffset;
                    cursorX -= wrapOffset;
                    cursorY += lineHeight;
                }
                else
                {
                    if (cursorX > maxX) maxX = cursorX;
                    cursorX = 0.0f;
                    cursorY += lineHeight;
                }
                wordStartGlyphIndex = result.glyphs.size();
                wordStartCursorX = 0.0f;
                prevCodepoint = 0;
            }

            LayoutGlyph lg;
            if (emitTofu)
            {
                const float advance = fontSize * TOFU_ADVANCE_EM;
                const float boxWidth = advance * TOFU_BOX_WIDTH_RATIO;
                const float boxHeight = block.ascenderPx * TOFU_BOX_HEIGHT_RATIO;
                lg.offset = glm::vec2(
                    cursorX + (advance - boxWidth) * 0.5f,
                    cursorY + block.ascenderPx - boxHeight);
                lg.size = glm::vec2(boxWidth, boxHeight);
                // The procedural shader interprets this as quad-local [0,1].
                lg.uvRect = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            }
            else
            {
                const float x = cursorX + glyph->bearingX * scale;
                // Fallbacks align to the style block's baseline without defining it.
                const float y =
                    cursorY + (faceCtx->face->metadata.ascender - glyph->bearingY) * scale +
                    faceCtx->baselineShift;
                const float w = glyph->atlasWidth * scale;
                const float h = glyph->atlasHeight * scale;

                const float u0 = static_cast<float>(glyph->atlasX) / faceCtx->atlasW;
                const float v0 = static_cast<float>(glyph->atlasY) / faceCtx->atlasH;
                const float u1 =
                    static_cast<float>(glyph->atlasX + glyph->atlasWidth) / faceCtx->atlasW;
                const float v1 =
                    static_cast<float>(glyph->atlasY + glyph->atlasHeight) / faceCtx->atlasH;

                lg.offset = glm::vec2(x, y);
                lg.size = glm::vec2(w, h);
                lg.uvRect = glm::vec4(u0, v0, u1, v1);
            }
            lg.codepoint = codepoint;
            lg.lineY = cursorY;
            lg.charIndex = charIndex;
            lg.faceIndex = faceIndex;
            result.glyphs.push_back(lg);

            cursorX += glyphAdvance;
            prevCodepoint = codepoint;
            prevFaceIndex = faceIndex;
        }

        if (cursorX > maxX) maxX = cursorX;
        result.boundingBox = glm::vec2(maxX, cursorY + lineHeight);

        return result;
    }

    std::vector<LineSpan> partitionLines(const std::vector<LayoutGlyph>& glyphs)
    {
        std::vector<LineSpan> lines;
        if (glyphs.empty()) return lines;

        LineSpan current;
        current.start = 0;
        current.count = 1;
        current.minX = glyphs[0].offset.x;
        current.maxX = glyphs[0].offset.x + glyphs[0].size.x;
        current.lineY = glyphs[0].lineY;

        for (size_t i = 1; i < glyphs.size(); ++i)
        {
            const auto& g = glyphs[i];
            if (std::abs(g.lineY - current.lineY) > 0.1f)
            {
                lines.push_back(current);
                current.start = i;
                current.count = 1;
                current.minX = g.offset.x;
                current.maxX = g.offset.x + g.size.x;
                current.lineY = g.lineY;
            }
            else
            {
                current.count++;
                current.minX = std::min(current.minX, g.offset.x);
                current.maxX = std::max(current.maxX, g.offset.x + g.size.x);
            }
        }
        lines.push_back(current);
        return lines;
    }

    void applyEllipsis(
        LayoutResult& layout,
        const resource::FontData& fontData,
        float fontSize,
        float maxWidth,
        float letterSpacing,
        float baselineShift)
    {
        if (layout.glyphs.empty() || maxWidth <= 0.0f ||
            fontData.metadata.baseFontSize == 0 ||
            fontData.atlas.width == 0 || fontData.atlas.height == 0)
        {
            return;
        }

        const float scale = fontSize / static_cast<float>(fontData.metadata.baseFontSize);
        const float atlasW = static_cast<float>(fontData.atlas.width);
        const float atlasH = static_cast<float>(fontData.atlas.height);

        // Resolve ellipsis: prefer U+2026, fall back to three U+002E dots
        const resource::GlyphData* ellipsisGlyph = fontData.findGlyph(0x2026);
        std::vector<const resource::GlyphData*> ellipsisGlyphs;
        if (ellipsisGlyph)
        {
            ellipsisGlyphs.push_back(ellipsisGlyph);
        }
        else
        {
            const resource::GlyphData* dot = fontData.findGlyph(0x002E);
            if (dot)
            {
                ellipsisGlyphs = {dot, dot, dot};
            }
        }

        // Compute total advance for the ellipsis run (with kerning between dots)
        float ellipsisAdvance = 0.0f;
        for (size_t i = 0; i < ellipsisGlyphs.size(); ++i)
        {
            ellipsisAdvance += ellipsisGlyphs[i]->advanceX * scale + letterSpacing;
            if (i + 1 < ellipsisGlyphs.size())
            {
                ellipsisAdvance += fontData.getKerning(
                    ellipsisGlyphs[i]->codepoint,
                    ellipsisGlyphs[i + 1]->codepoint) * scale;
            }
        }

        auto lines = partitionLines(layout.glyphs);
        std::vector<LayoutGlyph> rebuilt;
        rebuilt.reserve(layout.glyphs.size() + ellipsisGlyphs.size() * lines.size());

        // layoutText sets boundingBox.y = lastLineY + lineHeight, so the trailing line's
        // height falls straight out of the layout we were handed. Needed because dropping
        // a line below has to shrink the box, and a dropped line can be INTERIOR (short
        // lines survive while long ones vanish), so the count alone is not enough.
        const float lineHeight = layout.boundingBox.y - lines.back().lineY;

        float newMaxX = 0.0f;
        float lastKeptLineY = 0.0f;
        bool anyKept = false;

        for (const auto& line : lines)
        {
            const float lineWidth = line.maxX - line.minX;
            if (lineWidth <= maxWidth || ellipsisGlyphs.empty())
            {
                // Line fits, or we have no ellipsis to insert — keep as-is
                for (size_t i = 0; i < line.count; ++i)
                {
                    rebuilt.push_back(layout.glyphs[line.start + i]);
                }
                newMaxX = std::max(newMaxX, line.maxX);
                lastKeptLineY = line.lineY;
                anyKept = true;
                continue;
            }

            if (ellipsisAdvance > maxWidth)
            {
                // Even the ellipsis won't fit — drop the entire line
                continue;
            }

            // Keep longest glyph prefix whose width + ellipsisAdvance <= maxWidth.
            // Width-at-prefix-end is measured from line.minX to the source glyph's
            // (offset.x + advance), but LayoutGlyph doesn't store advance directly,
            // so we infer it from the next glyph's offset (or the glyph itself for the last).
            size_t keep = 0;
            float keepEndX = line.minX;
            for (size_t i = 0; i < line.count; ++i)
            {
                size_t g = line.start + i;
                float glyphEndX;
                if (i + 1 < line.count)
                {
                    glyphEndX = layout.glyphs[g + 1].offset.x;
                }
                else
                {
                    glyphEndX = layout.glyphs[g].offset.x + layout.glyphs[g].size.x;
                }
                if (glyphEndX - line.minX + ellipsisAdvance <= maxWidth)
                {
                    keep = i + 1;
                    keepEndX = glyphEndX;
                }
                else
                {
                    break;
                }
            }

            for (size_t i = 0; i < keep; ++i)
            {
                rebuilt.push_back(layout.glyphs[line.start + i]);
            }

            // Emit ellipsis glyph(s) starting at keepEndX, sharing the line origin
            float cursorX = keepEndX;
            for (size_t i = 0; i < ellipsisGlyphs.size(); ++i)
            {
                const resource::GlyphData* eg = ellipsisGlyphs[i];
                if (i > 0)
                {
                    cursorX += fontData.getKerning(
                        ellipsisGlyphs[i - 1]->codepoint,
                        eg->codepoint) * scale;
                }

                float x = cursorX + eg->bearingX * scale;
                float y = line.lineY + (fontData.metadata.ascender - eg->bearingY) * scale +
                          baselineShift;
                float w = eg->atlasWidth * scale;
                float h = eg->atlasHeight * scale;

                LayoutGlyph lg;
                lg.offset = glm::vec2(x, y);
                lg.size = glm::vec2(w, h);
                lg.uvRect = glm::vec4(
                    static_cast<float>(eg->atlasX) / atlasW,
                    static_cast<float>(eg->atlasY) / atlasH,
                    static_cast<float>(eg->atlasX + eg->atlasWidth) / atlasW,
                    static_cast<float>(eg->atlasY + eg->atlasHeight) / atlasH);
                lg.codepoint = eg->codepoint;
                lg.lineY = line.lineY;
                lg.charIndex = UINT32_MAX; // synthesized — renders with base style
                rebuilt.push_back(lg);

                cursorX += eg->advanceX * scale + letterSpacing;
            }

            newMaxX = std::max(newMaxX, cursorX);
            lastKeptLineY = line.lineY;
            anyKept = true;
        }

        layout.glyphs = std::move(rebuilt);
        // Both axes, or vertical alignment keeps sizing the block for lines that the
        // drop above deleted and the text sits off-centre inside its rect.
        layout.boundingBox = anyKept ? glm::vec2(newMaxX, lastKeptLineY + lineHeight)
                                     : glm::vec2(0.0f);
    }

    HAlign toHAlign(uint8_t value) noexcept
    {
        switch (value)
        {
        case 1: return HAlign::Center;
        case 2: return HAlign::Right;
        default: return HAlign::Left;
        }
    }

    VAlign toVAlign(uint8_t value) noexcept
    {
        switch (value)
        {
        case 1: return VAlign::Middle;
        case 2: return VAlign::Bottom;
        default: return VAlign::Top;
        }
    }

    LineMetrics computeLineMetrics(const resource::FontData& fontData,
                                   float fontSize,
                                   float lineSpacing)
    {
        LineMetrics metrics;
        if (fontData.metadata.baseFontSize == 0)
        {
            return metrics;
        }

        const float scale = fontSize / static_cast<float>(fontData.metadata.baseFontSize);
        metrics.lineHeight = fontData.metadata.lineHeight * scale * lineSpacing;

        // descender is negative (FreeType), so this is the ascent + descent em box.
        // Fall back to the full line height if the metadata is degenerate.
        const float emHeight = fontData.metadata.ascender - fontData.metadata.descender;
        metrics.singleLineHeight = (emHeight > 0.0f ? emHeight : fontData.metadata.lineHeight) * scale;

        return metrics;
    }

    LineMetrics computeLineMetrics(const FaceSet& faceSet, float fontSize, float lineSpacing)
    {
        // Per-slot metrics come from the single-face function, so a one-face set
        // returns exactly what the legacy call returned - alignment has to agree
        // with layoutTextStyled to the last bit or the block drifts inside its rect.
        LineMetrics metrics;
        bool any = false;

        for (const resource::FontData* face : faceSet.faces)
        {
            if (!faceUsable(face)) continue;

            const LineMetrics faceMetrics = computeLineMetrics(*face, fontSize, lineSpacing);
            if (!any)
            {
                metrics = faceMetrics;
                any = true;
            }
            else
            {
                metrics.lineHeight = std::max(metrics.lineHeight, faceMetrics.lineHeight);
                metrics.singleLineHeight =
                    std::max(metrics.singleLineHeight, faceMetrics.singleLineHeight);
            }
        }

        return metrics;
    }

    float baselineShiftForFaceSet(const FaceSet& faceSet, float fontSize)
    {
        if (!faceUsable(faceSet.faces[0])) return 0.0f;

        // buildBlockContext is the single source of truth for the shared baseline;
        // lineSpacing does not affect the ascender, so any value works here.
        const BlockContext block = buildBlockContext(faceSet, fontSize, 1.0f);
        return block.faces[0].baselineShift;
    }

    AlignmentOffsets computeAlignedLineOrigins(const LayoutResult& layout,
                                               const AlignParams& params)
    {
        AlignmentOffsets result;
        if (layout.glyphs.empty())
        {
            return result;
        }

        result.lines = partitionLines(layout.glyphs);
        result.lineOffsetX.assign(result.lines.size(), 0.0f);

        if (params.horizontal != HAlign::Left)
        {
            for (size_t i = 0; i < result.lines.size(); ++i)
            {
                const float lineWidth = result.lines[i].maxX - result.lines[i].minX;
                result.lineOffsetX[i] = (params.horizontal == HAlign::Center)
                                            ? (params.contentSize.x - lineWidth) * 0.5f
                                            : params.contentSize.x - lineWidth;
            }
        }

        if (params.vertical != VAlign::Top)
        {
            // VK-1632: boundingBox.y is the advance box - it counts a full (spaced)
            // lineHeight for the trailing line. For alignment the block ends at the
            // last line's em box instead, otherwise the leftover leading is dumped
            // below the text and Middle/Bottom sit high. The difference is zero for a
            // gapless font at lineSpacing 1, and half a line at lineSpacing 2.
            const float blockHeight = std::max(
                params.singleLineHeight,
                layout.boundingBox.y - params.lineHeight + params.singleLineHeight);

            result.offsetY = (params.vertical == VAlign::Middle)
                                 ? (params.contentSize.y - blockHeight) * 0.5f
                                 : params.contentSize.y - blockHeight;
        }

        return result;
    }

    void applyAlignment(LayoutResult& layout, const AlignParams& params)
    {
        if (params.horizontal == HAlign::Left && params.vertical == VAlign::Top)
        {
            return;
        }

        const AlignmentOffsets offsets = computeAlignedLineOrigins(layout, params);

        for (size_t i = 0; i < offsets.lines.size(); ++i)
        {
            const LineSpan& line = offsets.lines[i];
            for (size_t g = line.start; g < line.start + line.count; ++g)
            {
                layout.glyphs[g].offset.x += offsets.lineOffsetX[i];
                layout.glyphs[g].offset.y += offsets.offsetY;
            }
        }
    }

    OverflowMode toOverflowMode(uint8_t value) noexcept
    {
        switch (value)
        {
        case 1: return OverflowMode::Clip;
        case 2: return OverflowMode::Ellipsis;
        default: return OverflowMode::None;
        }
    }

    TextBoxPolicy resolveTextBox(const TextBoxRequest& request) noexcept
    {
        // NaN-safe: `!(x > 0)` is true for NaN, so a scripted NaN falls to the no-box
        // branch. `x <= 0` would be false and let NaN through into the layout.
        const bool hasWidth = request.maxWidth > 0.0f;
        const bool hasHeight = request.rectHeight > 0.0f;
        const OverflowMode overflow = toOverflowMode(request.overflow);

        TextBoxPolicy policy;

        // wordWrap gates wrapping only, never alignment: maxWidth stays the alignment box
        // either way. This is the same split UITextPipeline makes for UILabelComponent.
        policy.wrapWidth = request.wordWrap ? request.maxWidth : 0.0f;

        // Ellipsis is per-line, so it covers both wordWrap states: it truncates each
        // wrapped line, or the single un-wrapped one. It needs a width to truncate to.
        policy.ellipsis = (overflow == OverflowMode::Ellipsis) && hasWidth;
        policy.ellipsisWidth = request.maxWidth;

        // Clip has no implementation without a scissor, so it degrades to None rather
        // than being reinterpreted. The authored value is untouched and still round-trips.
        policy.clip = (overflow == OverflowMode::Clip) && request.clipSupported;

        policy.horizontal = toHAlign(request.horizontal);
        policy.vertical = (request.requireHeightForVAlign && !hasHeight)
                              ? VAlign::Top
                              : toVAlign(request.vertical);

        policy.alignToInkWidth = !hasWidth;
        policy.alignWidth = request.maxWidth;
        policy.alignHeight = request.rectHeight;

        return policy;
    }
}
