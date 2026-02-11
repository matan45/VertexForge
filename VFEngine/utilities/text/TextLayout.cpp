#include "TextLayout.hpp"
#include "../resource/Types.hpp"

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
        float lineSpacing)
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

            float glyphAdvance = glyph->advanceX * scale;

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
            result.glyphs.push_back(lg);

            cursorX += glyphAdvance;
            prevCodepoint = codepoint;
        }

        if (cursorX > maxX) maxX = cursorX;
        result.boundingBox = glm::vec2(maxX, cursorY + lineHeight);

        return result;
    }
}
