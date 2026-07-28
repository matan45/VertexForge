#pragma once
#include <string>
#include <string_view>

// VK-1630: the one place that decides what a font slot *says*, split out from
// the ImGui widget so it is unit-testable (the Tests project compiles no editor
// .cpp and has no ImGui context, but it does carry the VFEngine/editor include
// dir for header-only tests like test_contentbrowser_types.cpp).
//
// VK-1628 made every text emission site fall back to the engine default font
// (resource::fontPathOrDefault), and that fallback keys off resolve().empty(),
// NOT !isValid(). AssetRef::resolve() is empty in two very different authoring
// states: a null GUID (never assigned) and a live GUID whose AssetDatabase
// lookup and .vfmeta scan both miss (asset deleted, moved, or not imported).
// Both render with the default font; only the first is intentional, so the
// inspector must not collapse them into one reassuring "(Default)".

namespace windows::details
{
    enum class FontSlotState
    {
        Unset,     // null GUID -> engine default at render, by design
        Assigned,  // GUID resolves to a file
        Missing    // GUID set but unresolvable -> default at render, but a content bug
    };

    // "(Default)" is the established vocabulary for "no explicit override, a
    // fallback is substituted" (MaterialDrawer.cpp submesh material override).
    // "(None)" is reserved for slots that have no fallback at all.
    inline constexpr const char* FONT_SLOT_DEFAULT_LABEL = "(Default)";
    inline constexpr const char* FONT_SLOT_MISSING_LABEL = "(missing - using default)";

    inline constexpr const char* FONT_SLOT_DEFAULT_TOOLTIP =
        "No font assigned. Text renders with the built-in default font.";
    inline constexpr const char* FONT_SLOT_MISSING_TOOLTIP =
        "This font asset could not be found (deleted, moved, or not imported). "
        "Text falls back to the built-in default font.";

    // Takes the two observable facts about an asset::AssetRef as plain values on
    // purpose: resolve() on a Missing ref is an uncached recursive .vfmeta scan
    // (AssetDatabase::tryResolveByMetaScan), so callers must resolve ONCE and
    // hand the result in rather than letting this call resolve() a second time.
    [[nodiscard]] constexpr FontSlotState fontSlotState(bool refIsValid,
                                                        std::string_view resolvedPath) noexcept
    {
        if (!refIsValid)
        {
            return FontSlotState::Unset;
        }
        return resolvedPath.empty() ? FontSlotState::Missing : FontSlotState::Assigned;
    }

    // Basename of a resolved asset path. AssetDatabase normalizes to forward
    // slashes, but .vfmeta-scan results can carry native separators, so accept
    // both — same as the per-drawer code this replaces.
    [[nodiscard]] inline std::string fontSlotFileName(std::string_view resolvedPath)
    {
        const auto lastSlash = resolvedPath.find_last_of("/\\");
        return std::string(lastSlash == std::string_view::npos
                               ? resolvedPath
                               : resolvedPath.substr(lastSlash + 1));
    }
}
