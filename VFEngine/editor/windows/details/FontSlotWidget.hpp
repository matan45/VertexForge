#pragma once
#include "FontSlotDisplay.hpp"
#include "asset/AssetRef.hpp"

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
}
