#pragma once
#include "../asset/AssetRef.hpp"
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
