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

    LayoutResult layoutText(
        const resource::FontData& fontData,
        const std::string& text,
        float fontSize,
        float maxWidth,
        float lineSpacing,
        float letterSpacing)
    {
        LayoutResult result;

        if (text.empty() || fontData.metadata.baseFontSize == 0 ||
            fontData.atlas.width == 0 || fontData.atlas.height == 0)
        {
            return result;
        }

        float scale = fontSize / static_cast<float>(fontData.metadata.baseFontSize);
        float lineHeight = fontData.metadata.lineHeight * scale * lineSpacing;

        float cursorX = 0.0f;
        float cursorY = 0.0f;
        float maxX = 0.0f;

        uint32_t prevCodepoint = 0;
        size_t i = 0;

        // Track word boundaries for word wrap
        size_t wordStartGlyphIndex = 0;
        float wordStartCursorX = 0.0f;

        while (i < text.size())
        {
            uint32_t codepoint = decodeUTF8(text, i);

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

            const resource::GlyphData* glyph = fontData.findGlyph(codepoint);
            if (!glyph)
            {
                cursorX += fontSize * 0.5f;
                prevCodepoint = codepoint;
                continue;
            }

            if (prevCodepoint != 0)
            {
                cursorX += fontData.getKerning(prevCodepoint, codepoint) * scale;
            }

            float glyphAdvance = glyph->advanceX * scale + letterSpacing;

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
                    }
                    if (cursorX - wrapOffset > maxX) maxX = wrapOffset;
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

            float x = cursorX + glyph->bearingX * scale;
            float y = cursorY + (fontData.metadata.ascender - glyph->bearingY) * scale;
            float w = glyph->atlasWidth * scale;
            float h = glyph->atlasHeight * scale;

            float atlasW = static_cast<float>(fontData.atlas.width);
            float atlasH = static_cast<float>(fontData.atlas.height);

            float u0 = static_cast<float>(glyph->atlasX) / atlasW;
            float v0 = static_cast<float>(glyph->atlasY) / atlasH;
            float u1 = static_cast<float>(glyph->atlasX + glyph->atlasWidth) / atlasW;
            float v1 = static_cast<float>(glyph->atlasY + glyph->atlasHeight) / atlasH;

            LayoutGlyph lg;
            lg.offset = glm::vec2(x, y);
            lg.size = glm::vec2(w, h);
            lg.uvRect = glm::vec4(u0, v0, u1, v1);
            lg.codepoint = codepoint;
            lg.lineY = cursorY;
            result.glyphs.push_back(lg);

            cursorX += glyphAdvance;
            prevCodepoint = codepoint;
        }

        if (cursorX > maxX) maxX = cursorX;
        result.boundingBox = glm::vec2(maxX, cursorY + lineHeight);

        return result;
    }

    namespace
    {
        struct LineSpan
        {
            size_t start = 0;
            size_t count = 0;
            float minX = 0.0f;
            float maxX = 0.0f;
            float lineY = 0.0f;  // cursorY of this line's origin
        };

        // Group glyphs by their lineY (the line origin recorded at layout time).
        // Critically NOT by offset.y, which varies per glyph based on bearingY
        // and would split a single visual line into one fake "line" per bearingY.
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
    }

    void applyEllipsis(
        LayoutResult& layout,
        const resource::FontData& fontData,
        float fontSize,
        float maxWidth,
        float letterSpacing)
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

        float newMaxX = 0.0f;

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
                float y = line.lineY + (fontData.metadata.ascender - eg->bearingY) * scale;
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
                rebuilt.push_back(lg);

                cursorX += eg->advanceX * scale + letterSpacing;
            }

            newMaxX = std::max(newMaxX, cursorX);
        }

        layout.glyphs = std::move(rebuilt);
        layout.boundingBox.x = newMaxX;
    }
}
