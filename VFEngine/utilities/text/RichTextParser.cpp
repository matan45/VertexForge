#include "RichTextParser.hpp"
#include "TextLayout.hpp"
#include <algorithm>
#include <cctype>

namespace text
{
    namespace
    {
        struct ParseState
        {
            int boldDepth = 0;
            int italicDepth = 0;
            std::vector<glm::vec4> colorStack;

            RichTextSpanStyle currentStyle() const
            {
                RichTextSpanStyle style;
                if (boldDepth > 0) style.styleFlags |= 0x1;
                if (italicDepth > 0) style.styleFlags |= 0x2;
                if (!colorStack.empty())
                {
                    style.hasColor = true;
                    style.color = colorStack.back();
                }
                return style;
            }
        };

        bool parseHexColor(const std::string& hex, glm::vec4& outColor)
        {
            if (hex.size() != 7 && hex.size() != 9) return false;
            if (hex[0] != '#') return false;

            auto hexVal = [](char c) -> int
            {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };

            uint32_t channels[4] = {0, 0, 0, 255};
            size_t channelCount = (hex.size() - 1) / 2;
            for (size_t c = 0; c < channelCount; ++c)
            {
                int hi = hexVal(hex[1 + c * 2]);
                int lo = hexVal(hex[2 + c * 2]);
                if (hi < 0 || lo < 0) return false;
                channels[c] = static_cast<uint32_t>(hi * 16 + lo);
            }

            outColor = {channels[0] / 255.0f, channels[1] / 255.0f,
                        channels[2] / 255.0f, channels[3] / 255.0f};
            return true;
        }

        // Appends a raw text segment, pushing one style entry per decoded
        // codepoint — the exact enumeration layoutText will use later.
        void appendSegment(RichTextResult& result, const std::string& segment,
                           const RichTextSpanStyle& style)
        {
            if (segment.empty()) return;
            size_t k = 0;
            while (k < segment.size())
            {
                decodeUTF8(segment, k);
                result.perCodepoint.push_back(style);
            }
            result.strippedText += segment;
        }
    }

    RichTextResult parseRichText(const std::string& markup)
    {
        RichTextResult result;
        result.strippedText.reserve(markup.size());
        result.perCodepoint.reserve(markup.size());

        ParseState state;

        size_t i = 0;
        size_t plainStart = 0; // start of the pending plain-text run

        auto flushPlain = [&](size_t end)
        {
            if (end > plainStart)
            {
                appendSegment(result, markup.substr(plainStart, end - plainStart),
                              state.currentStyle());
            }
        };

        while (i < markup.size())
        {
            if (markup[i] != '[')
            {
                ++i;
                continue;
            }

            // Escaped literal: "[[" -> "["
            if (i + 1 < markup.size() && markup[i + 1] == '[')
            {
                flushPlain(i);
                appendSegment(result, "[", state.currentStyle());
                i += 2;
                plainStart = i;
                continue;
            }

            size_t close = markup.find(']', i + 1);
            if (close == std::string::npos)
            {
                // No closing bracket — the rest renders literally
                break;
            }

            std::string tag = markup.substr(i + 1, close - i - 1);
            bool consumed = true;
            // The pending plain run must flush with the style that was active
            // while it was written, not the style this tag switches to.
            RichTextSpanStyle styleBeforeTag = state.currentStyle();

            if (tag == "b")
            {
                state.boldDepth++;
            }
            else if (tag == "/b")
            {
                if (state.boldDepth > 0) state.boldDepth--;
                else consumed = false; // unmatched close renders literally
            }
            else if (tag == "i")
            {
                state.italicDepth++;
            }
            else if (tag == "/i")
            {
                if (state.italicDepth > 0) state.italicDepth--;
                else consumed = false;
            }
            else if (tag.rfind("color=", 0) == 0)
            {
                glm::vec4 color;
                if (parseHexColor(tag.substr(6), color))
                {
                    state.colorStack.push_back(color);
                }
                else
                {
                    consumed = false;
                }
            }
            else if (tag == "/color")
            {
                if (!state.colorStack.empty()) state.colorStack.pop_back();
                else consumed = false;
            }
            else if (tag.rfind("icon=", 0) == 0)
            {
                // Reserved for inline icons — consumed, emits nothing
            }
            else
            {
                consumed = false;
            }

            if (consumed)
            {
                if (i > plainStart)
                {
                    appendSegment(result, markup.substr(plainStart, i - plainStart), styleBeforeTag);
                }
                i = close + 1;
                plainStart = i;
            }
            else
            {
                // Not a recognized tag — leave it in the plain-text run
                i = close + 1;
            }
        }

        flushPlain(markup.size());
        return result;
    }
}
