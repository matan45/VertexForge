#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace text
{
    // Four fallback slots fit the existing uint8_t face-index encoding:
    // style faces are 0..3, fallbacks are 4..7, and tofu is the sentinel below.
    // The built-in default font is a mandatory tail, leaving three authored slots.
    inline constexpr uint8_t MAX_FALLBACK_FACES = 4;
    inline constexpr uint8_t MAX_AUTHORED_FALLBACKS = MAX_FALLBACK_FACES - 1;
    inline constexpr uint8_t TOFU_FACE_INDEX = 0xFF;

    inline constexpr float TOFU_ADVANCE_EM = 0.5f;
    inline constexpr float TOFU_BOX_WIDTH_RATIO = 0.80f;
    inline constexpr float TOFU_BOX_HEIGHT_RATIO = 0.72f;

    enum class MissingClass : uint8_t
    {
        Printing = 0,
        SpaceLike = 1,
        ZeroWidth = 2
    };

    // Classifies a codepoint only after every face in the fallback chain missed it.
    // SpaceLike deliberately precedes the general control ranges: TAB must retain
    // the historical half-em advance, and U+0020 must never become visible tofu.
    [[nodiscard]] constexpr MissingClass missingClass(uint32_t codepoint) noexcept
    {
        if (codepoint == 0x0009 || codepoint == 0x0020 || codepoint == 0x00A0 ||
            codepoint == 0x1680 || (codepoint >= 0x2000 && codepoint <= 0x200A) ||
            codepoint == 0x2028 || codepoint == 0x2029 || codepoint == 0x202F ||
            codepoint == 0x205F || codepoint == 0x3000)
        {
            return MissingClass::SpaceLike;
        }

        if (codepoint <= 0x001F || (codepoint >= 0x007F && codepoint <= 0x009F) ||
            codepoint == 0x00AD || codepoint == 0x180E ||
            (codepoint >= 0x200B && codepoint <= 0x200F) ||
            (codepoint >= 0x202A && codepoint <= 0x202E) ||
            (codepoint >= 0x2060 && codepoint <= 0x2064) ||
            (codepoint >= 0x2066 && codepoint <= 0x2069) ||
            (codepoint >= 0xFE00 && codepoint <= 0xFE0F) ||
            codepoint == 0xFEFF ||
            (codepoint >= 0xFFF9 && codepoint <= 0xFFFB) ||
            (codepoint >= 0xE0000 && codepoint <= 0xE01EF))
        {
            return MissingClass::ZeroWidth;
        }

        return MissingClass::Printing;
    }

    // A fixed-capacity, allocation-free normalized view over authored paths.
    // Views refer either to the caller-owned strings or to the static default
    // sentinel, so callers that retain the result must first copy/intern them.
    struct NormalizedFallbackChain
    {
        std::array<std::string_view, MAX_FALLBACK_FACES> paths{};
        uint8_t count = 0;
    };

    // ASCII-trim -> drop empties -> drop primary -> first-wins dedup -> append
    // the built-in default sentinel. Path identity treats '\\' and '/' equally;
    // spelling remains a view into the source and can be canonicalized when interned.
    [[nodiscard]] NormalizedFallbackChain normalizeFallbackChain(
        std::span<const std::string> authored,
        std::string_view primaryPath = {}) noexcept;
}
