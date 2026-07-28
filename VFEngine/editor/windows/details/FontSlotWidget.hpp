#pragma once
#include "FontSlotDisplay.hpp"
#include "asset/AssetRef.hpp"
#include <functional>
#include <string>
#include <string_view>

namespace windows::details
{
    struct FontSlotOptions
    {
        // Shown (disabled) when no font is assigned. Override only where the
        // render-side fallback is not a plain "use the default font" — see
        // UIFrameBuilder::findDropdownFont, which inherits from this entity's
        // UI Label and then a child UI Label before it reaches the default.
        const char* emptyLabel = FONT_SLOT_DEFAULT_LABEL;
        const char* emptyTooltip = FONT_SLOT_DEFAULT_TOOLTIP;
        // Lets path-backed callers preserve a missing assignment even though
        // there is no valid AssetRef to carry it.
        std::string_view missingPath;
        const char* clearButtonLabel = "Use Default";
        const char* clearTooltip =
            "Drop the reference; text renders with the built-in default font.";
        // Runs before AssetRef::fromPath, allowing project settings to reject
        // files outside the project without creating metadata for them.
        std::function<bool(std::string_view)> pathValidator;
    };

    // Renders one font reference slot: state line (also a .vfFont drop target
    // for content-browser drags) + "Select Font" file dialog + "Use Default".
    //
    // Returns true only when fontRef actually changed, preserving the drawers'
    // bool-changed contract (EntityDetailsPanel dispatches a Set*DataCommand on
    // it; UILayerBuilderWindow additionally folds it into an undo entry).
    //
    // idSuffix scopes every ImGui ID inside via PushID, so callers no longer
    // carry per-drawer "##Xxx" suffixes.
    bool drawFontSlot(asset::AssetRef& fontRef, const char* idSuffix,
                      const FontSlotOptions& opts = {});

    // ProjectConfig persists relative strings rather than GUID-backed
    // AssetRefs. This wrapper safely bridges that representation while keeping
    // missing entries visible and rejecting selections outside projectRoot.
    bool drawProjectFontSlot(std::string& projectRelativePath,
                             std::string_view projectRoot,
                             const char* idSuffix,
                             const FontSlotOptions& opts = {});
}
