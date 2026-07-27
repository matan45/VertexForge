#include "RichTextParser.hpp"
#include "TextLayout.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>

namespace text
{
    namespace
    {
        // VK-1635: one entry per open effect tag. Separate stacks rather than one struct
        // so [outline] and [shadow] nest independently, the way [b] and [i] already do.
        struct OutlineSpan
        {
            glm::vec4 color;
            float width;
        };

        struct ShadowSpan
        {
            glm::vec4 color;
            glm::vec2 offset;
        };

        struct GlowSpan
        {
            glm::vec4 color;
            float range;
        };

        struct ParseState
        {
            int boldDepth = 0;
            int italicDepth = 0;
            std::vector<glm::vec4> colorStack;
            std::vector<OutlineSpan> outlineStack;
            std::vector<ShadowSpan> shadowStack;
            std::vector<GlowSpan> glowStack;

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
                if (!outlineStack.empty())
                {
                    style.hasOutline = true;
                    style.outlineColor = outlineStack.back().color;
                    style.outlineWidth = outlineStack.back().width;
                }
                if (!shadowStack.empty())
                {
                    style.hasShadow = true;
                    style.shadowColor = shadowStack.back().color;
                    style.shadowOffset = shadowStack.back().offset;
                }
                if (!glowStack.empty())
                {
                    style.hasGlow = true;
                    style.glowColor = glowStack.back().color;
                    style.glowRange = glowStack.back().range;
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

        // VK-1635: parses an effect tag's value - "#RRGGBB(AA)" optionally followed by up
        // to maxNumbers comma-separated numbers, e.g. "#000000FF,2.5" or "#000,1,1".
        //
        // Every field must parse in full: a partially-consumed number ("2px") is rejected
        // rather than silently read as 2, so a typo renders literally like any other
        // malformed tag instead of quietly producing the wrong outline.
        //
        // Numbers not supplied are left at 0, which the RichTextSpanStyle::applyTo()
        // fallbacks read as "not given".
        bool parseEffectTagValue(const std::string& value, size_t maxNumbers,
                                 glm::vec4& outColor, float* outNumbers)
        {
            size_t comma = value.find(',');
            if (!parseHexColor(value.substr(0, comma), outColor))
            {
                return false;
            }

            size_t index = 0;
            while (comma != std::string::npos)
            {
                if (index >= maxNumbers)
                {
                    return false; // more numbers than this tag accepts
                }
                const size_t start = comma + 1;
                comma = value.find(',', start);
                const size_t end = (comma == std::string::npos) ? value.size() : comma;
                if (end <= start)
                {
                    return false; // empty field
                }

                float parsed = 0.0f;
                const char* first = value.data() + start;
                const char* last = value.data() + end;
                const auto [ptr, ec] = std::from_chars(first, last, parsed);
                if (ec != std::errc{} || ptr != last)
                {
                    return false;
                }
                outNumbers[index++] = parsed;
            }

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
            else if (tag.rfind("outline=", 0) == 0)
            {
                glm::vec4 color;
                float numbers[1] = {0.0f};
                if (parseEffectTagValue(tag.substr(8), 1, color, numbers))
                {
                    state.outlineStack.push_back({color, numbers[0]});
                }
                else
                {
                    consumed = false;
                }
            }
            else if (tag == "/outline")
            {
                if (!state.outlineStack.empty()) state.outlineStack.pop_back();
                else consumed = false;
            }
            else if (tag == "shadow")
            {
                // Bare [shadow]: inherits the label's colour and offset, or falls back to
                // the RICH_TEXT_DEFAULT_* constants when the label has no shadow at all.
                // Resolved in RichTextSpanStyle::applyTo(), which is the only place that
                // can see the label's own settings.
                state.shadowStack.push_back({glm::vec4(0.0f, 0.0f, 0.0f, 0.5f), glm::vec2(0.0f)});
            }
            else if (tag.rfind("shadow=", 0) == 0)
            {
                glm::vec4 color;
                float numbers[2] = {0.0f, 0.0f};
                if (parseEffectTagValue(tag.substr(7), 2, color, numbers))
                {
                    state.shadowStack.push_back({color, glm::vec2(numbers[0], numbers[1])});
                }
                else
                {
                    consumed = false;
                }
            }
            else if (tag == "/shadow")
            {
                if (!state.shadowStack.empty()) state.shadowStack.pop_back();
                else consumed = false;
            }
            else if (tag.rfind("glow=", 0) == 0)
            {
                glm::vec4 color;
                float numbers[1] = {0.0f};
                if (parseEffectTagValue(tag.substr(5), 1, color, numbers))
                {
                    state.glowStack.push_back({color, numbers[0]});
                }
                else
                {
                    consumed = false;
                }
            }
            else if (tag == "/glow")
            {
                if (!state.glowStack.empty()) state.glowStack.pop_back();
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
