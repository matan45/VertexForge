#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <cstdint>

namespace text
{
    // Per-codepoint style resolved from rich text markup.
    // styleFlags matches the UI text pipeline: bit0 = bold, bit1 = italic.
    struct RichTextSpanStyle
    {
        uint32_t styleFlags = 0;
        bool hasColor = false;
        glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    };

    struct RichTextResult
    {
        std::string strippedText;
        // One entry per decoded codepoint of strippedText, in the same
        // enumeration order layoutText uses (every decodeUTF8 result counts).
        std::vector<RichTextSpanStyle> perCodepoint;
    };

    // Parses a BBCode subset: [b]..[/b], [i]..[/i], [color=#RRGGBB]..[/color]
    // (also #RRGGBBAA). Tags nest; [[ escapes a literal '['. Unknown or
    // malformed tags render literally. [icon=...] is consumed and skipped
    // (reserved for a future inline-icon feature).
    RichTextResult parseRichText(const std::string& markup);
}
