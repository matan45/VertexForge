#pragma once
#include "../asset/AssetRef.hpp"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>

// UI theme data model: a .vfTheme asset is a set of named styles, each a
// property bag keyed by well-known property names (see UIThemeApplier for the
// property -> component field mapping). Only properties present in a style are
// applied; everything else keeps its authored per-widget value.
namespace utilities::ui
{
    struct UIThemeStyle
    {
        std::unordered_map<std::string, glm::vec4> colors;
        std::unordered_map<std::string, float> floats;
        std::unordered_map<std::string, asset::AssetRef> assets;
    };

    struct UITheme
    {
        std::unordered_map<std::string, UIThemeStyle> styles;

        const UIThemeStyle* findStyle(const std::string& key) const
        {
            auto it = styles.find(key);
            return it != styles.end() ? &it->second : nullptr;
        }
    };
}
