#pragma once
#include "EditorTheme.hpp"
#include <optional>
#include <string>
#include <vector>

namespace config
{
    // File-based persistence for custom editor themes. One JSON file per theme,
    // named after the sanitized theme name, under defaultThemesDirectory()
    // (an explicit directory parameter keeps the class unit-testable).
    class EditorThemeStorage
    {
    public:
        static std::vector<std::string> listThemes(const std::string& dir);
        static std::optional<EditorTheme> load(const std::string& dir, const std::string& name);
        static bool save(const std::string& dir, const EditorTheme& theme);
        static bool remove(const std::string& dir, const std::string& name);

        // Strips characters not in [alnum, space, dash, underscore] and trims spaces.
        // Returns "" for empty results and the reserved built-in names "Dark"/"Light".
        static std::string sanitizeName(const std::string& name);

        static std::string defaultThemesDirectory();
    };
}
