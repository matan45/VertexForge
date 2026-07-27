#include "FontStyleFace.hpp"

namespace text
{
    namespace
    {
        // Suffixes inserted between the stem and the extension, most idiomatic first.
        // Windows is case-insensitive so the lower-case spellings are usually
        // redundant, but they cost one memoised probe each and keep the convention
        // honest if the engine is ever built against a case-sensitive filesystem.
        std::span<const std::string_view> suffixesFor(uint32_t styleBits)
        {
            static constexpr std::string_view boldSuffixes[] = {
                "-Bold", "_Bold", "Bold", "-bold", "_bold"};
            static constexpr std::string_view italicSuffixes[] = {
                "-Italic", "_Italic", "Italic", "-Oblique", "_Oblique", "-italic", "_italic"};
            static constexpr std::string_view boldItalicSuffixes[] = {
                "-BoldItalic", "_BoldItalic", "-Bold-Italic", "BoldItalic",
                "-BoldOblique", "_BoldOblique", "-bolditalic"};

            switch (styleBits & STYLE_MASK)
            {
            case STYLE_BOLD: return boldSuffixes;
            case STYLE_ITALIC: return italicSuffixes;
            case STYLE_BOLD | STYLE_ITALIC: return boldItalicSuffixes;
            default: return {};
            }
        }

        // Index of the extension dot, or npos. Only a dot AFTER the last separator
        // counts, so "fonts.v2/Roboto" is treated as extensionless rather than having
        // an extension of "v2/Roboto".
        size_t extensionDot(std::string_view path)
        {
            const size_t lastSep = path.find_last_of("/\\");
            const size_t dot = path.find_last_of('.');
            if (dot == std::string_view::npos) return std::string_view::npos;
            if (lastSep != std::string_view::npos && dot < lastSep) return std::string_view::npos;
            // A leading-dot file name (".vfFont") is all extension and has no stem to
            // suffix; treat it as extensionless so the suffix still appends.
            const size_t nameStart = (lastSep == std::string_view::npos) ? 0 : lastSep + 1;
            if (dot == nameStart) return std::string_view::npos;
            return dot;
        }
    }

    std::vector<uint32_t> styleDowngradeOrder(uint32_t styleBits)
    {
        switch (styleBits & STYLE_MASK)
        {
        case STYLE_BOLD: return {STYLE_BOLD};
        case STYLE_ITALIC: return {STYLE_ITALIC};
        case STYLE_BOLD | STYLE_ITALIC:
            return {STYLE_BOLD | STYLE_ITALIC, STYLE_BOLD, STYLE_ITALIC};
        default: return {};
        }
    }

    std::vector<std::string> styledPathCandidates(std::string_view basePath, uint32_t styleBits)
    {
        std::vector<std::string> candidates;
        if (basePath.empty()) return candidates;

        const std::span<const std::string_view> suffixes = suffixesFor(styleBits);
        if (suffixes.empty()) return candidates;

        const size_t dot = extensionDot(basePath);
        const std::string_view stem =
            (dot == std::string_view::npos) ? basePath : basePath.substr(0, dot);
        const std::string_view extension =
            (dot == std::string_view::npos) ? std::string_view{} : basePath.substr(dot);

        candidates.reserve(suffixes.size());
        for (const std::string_view suffix : suffixes)
        {
            std::string candidate;
            candidate.reserve(stem.size() + suffix.size() + extension.size());
            candidate.append(stem).append(suffix).append(extension);
            candidates.push_back(std::move(candidate));
        }
        return candidates;
    }

    StyleChoice chooseStyleFace(uint32_t requestedBits,
                                std::span<const StyleFaceProbe> probes) noexcept
    {
        StyleChoice choice;
        choice.synthesizedBits = requestedBits & STYLE_MASK;
        if (choice.synthesizedBits == 0) return choice;

        for (size_t i = 0; i < probes.size(); ++i)
        {
            const StyleFaceProbe& probe = probes[i];
            if (!probe.exists || !probe.resident) continue;

            choice.index = static_cast<int>(i);
            choice.synthesizedBits &= ~probe.satisfiedBits;
            return choice;
        }

        return choice;
    }
}
