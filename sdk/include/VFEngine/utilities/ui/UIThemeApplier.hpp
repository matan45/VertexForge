#pragma once
#include "UITheme.hpp"
#include <entt/entt.hpp>

namespace utilities::ui
{
    // Writes theme style properties into concrete component fields for every
    // entity carrying a UIStyleComponent in the given canvas subtree. Render
    // and interaction code keep reading the plain component fields; reskinning
    // is just re-running this pass.
    //
    // Well-known property names per style:
    //   colors: labelColor, imageTint,
    //           buttonNormalColor, buttonHoveredColor, buttonPressedColor, buttonDisabledColor,
    //           progressTrackColor, progressFillColor
    //   floats: fontSize
    //   assets: font, imageTexture,
    //           buttonNormalTexture, buttonHoveredTexture, buttonPressedTexture, buttonDisabledTexture
    class UIThemeApplier
    {
    public:
        // Applies to canvasRoot's whole subtree (canvasRoot itself included).
        // Returns the number of styled entities touched.
        static int applyTheme(entt::registry& registry, const UITheme& theme, entt::entity canvasRoot);

        // Applies a single style to one entity (used when a styleKey changes).
        static bool applyStyleToEntity(entt::registry& registry, const UIThemeStyle& style, entt::entity entity);
    };
}
