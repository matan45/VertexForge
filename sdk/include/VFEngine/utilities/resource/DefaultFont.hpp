#pragma once
#include "../asset/AssetRef.hpp"
#include <cstdint>
#include <string>

// VK-1628: the engine ships one default font so text never silently disappears.
//
// A UILabel/TextComponent whose fontRef was never assigned used to be dropped at
// gather time and render nothing. The gather sites now substitute the sentinel
// below, which TextFontCache resolves to a builtin loaded straight off disk.
//
// Why a sentinel and not the real path: TextFontCache::requestFont turns the path
// back into an asset::AssetRef (AssetRef::fromPath), which normalizes against the
// PROJECT root and auto-registers unknown files by writing a .vfmeta next to them.
// An engine resource lives outside any project, so that would either mis-resolve
// or pollute resources/. The sentinel short-circuits before that ever happens -
// exactly how "__white_1x1__" works for missing UI textures (UIRenderPipeline.cpp).

namespace resource
{
    // Cache key, NOT a filesystem path. Never hand this to any file API.
    inline constexpr const char* DEFAULT_FONT_SENTINEL = "__default_font__";

    // Development-relative engine path. MUST keep the "../../resources/" prefix
    // verbatim: PathResolver::resolveEnginePath byte-compares against it to rewrite
    // the path to "resources/..." in an exported build, which is also the .vfpak
    // archive key GameExporter produces. Any other spelling silently skips the
    // rewrite and breaks only the shipped game.
    inline constexpr const char* DEFAULT_FONT_ENGINE_PATH = "../../resources/fonts/DefaultFont.vfFont";

    // VK-1636: the default font's styled faces. The base font is keyed by a sentinel
    // rather than a path, so it has no filesystem siblings for the naming convention
    // to find - these stand in for them. Cache keys, NOT paths, exactly like the base
    // sentinel: TextFontCache::requestFont must early-out on all four.
    //
    // None of these three files ships yet. TextFontCache probes for them once at
    // init() and silently keeps synthesizing when they are absent, so dropping the
    // OFL Roboto Bold / Italic / BoldItalic faces into resources/fonts/ later needs
    // no code change at all. Keep the "../../resources/" prefix verbatim, same
    // reason as above.
    inline constexpr const char* DEFAULT_FONT_BOLD_SENTINEL = "__default_font_bold__";
    inline constexpr const char* DEFAULT_FONT_ITALIC_SENTINEL = "__default_font_italic__";
    inline constexpr const char* DEFAULT_FONT_BOLD_ITALIC_SENTINEL = "__default_font_bold_italic__";

    inline constexpr const char* DEFAULT_FONT_BOLD_ENGINE_PATH =
        "../../resources/fonts/DefaultFont-Bold.vfFont";
    inline constexpr const char* DEFAULT_FONT_ITALIC_ENGINE_PATH =
        "../../resources/fonts/DefaultFont-Italic.vfFont";
    inline constexpr const char* DEFAULT_FONT_BOLD_ITALIC_ENGINE_PATH =
        "../../resources/fonts/DefaultFont-BoldItalic.vfFont";

    // Cache key for a styled default face, indexed by the text::STYLE_* bits
    // (1 = bold, 2 = italic, 3 = both). Returns the base sentinel for 0 or anything
    // out of range, so a caller can never accidentally build a file path from it.
    inline constexpr const char* defaultFontSentinelForStyle(uint32_t styleBits)
    {
        switch (styleBits)
        {
        case 1: return DEFAULT_FONT_BOLD_SENTINEL;
        case 2: return DEFAULT_FONT_ITALIC_SENTINEL;
        case 3: return DEFAULT_FONT_BOLD_ITALIC_SENTINEL;
        default: return DEFAULT_FONT_SENTINEL;
        }
    }

    // Engine-relative source for the styled default face with those bits, or nullptr.
    inline constexpr const char* defaultFontEnginePathForStyle(uint32_t styleBits)
    {
        switch (styleBits)
        {
        case 1: return DEFAULT_FONT_BOLD_ENGINE_PATH;
        case 2: return DEFAULT_FONT_ITALIC_ENGINE_PATH;
        case 3: return DEFAULT_FONT_BOLD_ITALIC_ENGINE_PATH;
        default: return nullptr;
        }
    }

    // True for the base sentinel and all three styled ones. Anything this returns
    // true for must never reach a file API or asset::AssetRef::fromPath.
    inline bool isDefaultFontSentinel(const std::string& key)
    {
        return key == DEFAULT_FONT_SENTINEL || key == DEFAULT_FONT_BOLD_SENTINEL ||
               key == DEFAULT_FONT_ITALIC_SENTINEL || key == DEFAULT_FONT_BOLD_ITALIC_SENTINEL;
    }

    // Resolved font path for a component reference, falling back to the default.
    // AssetRef::resolve() returns "" both for a null GUID (no font assigned) and
    // for a GUID whose database lookup + .vfmeta scan both miss, so one emptiness
    // test covers every reference-side failure.
    inline std::string fontPathOrDefault(const asset::AssetRef& ref)
    {
        const std::string& resolved = ref.resolve();
        return resolved.empty() ? std::string(DEFAULT_FONT_SENTINEL) : resolved;
    }
}
