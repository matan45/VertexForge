#pragma once
#include "UITheme.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace utilities::ui
{
    // .vfTheme JSON <-> UITheme. Lives in Utilities (static lib) so the
    // services layer, editor, and tests can all use it without DLL exports.
    class UIThemeSerialization
    {
    public:
        static nlohmann::json toJson(const UITheme& theme);
        static UITheme fromJson(const nlohmann::json& j);

        static bool saveToFile(const UITheme& theme, const std::string& filePath);
        static std::optional<UITheme> loadFromFile(const std::string& filePath);
    };
}
