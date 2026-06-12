#include "EditorThemeStorage.hpp"
#include "EditorThemeSerializer.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace config
{
    namespace
    {
        std::filesystem::path themeFilePath(const std::string& dir, const std::string& name)
        {
            return std::filesystem::path(dir) / (name + ".json");
        }
    }

    std::vector<std::string> EditorThemeStorage::listThemes(const std::string& dir)
    {
        std::vector<std::string> names;
        std::error_code ec;
        if (dir.empty() || !std::filesystem::is_directory(dir, ec))
            return names;

        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (!entry.is_regular_file() || entry.path().extension() != ".json")
                continue;
            names.push_back(entry.path().stem().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }

    std::optional<EditorTheme> EditorThemeStorage::load(const std::string& dir, const std::string& name)
    {
        std::string fileName = sanitizeName(name);
        if (fileName.empty())
            return std::nullopt;

        try
        {
            std::ifstream file(themeFilePath(dir, fileName));
            if (!file.is_open())
                return std::nullopt;

            nlohmann::json j = nlohmann::json::parse(file);
            EditorTheme theme = j.get<EditorTheme>();
            if (theme.name.empty())
                theme.name = fileName;
            return theme;
        }
        catch (const std::exception&)
        {
            return std::nullopt;
        }
    }

    bool EditorThemeStorage::save(const std::string& dir, const EditorTheme& theme)
    {
        std::string fileName = sanitizeName(theme.name);
        if (dir.empty() || fileName.empty())
            return false;

        try
        {
            std::filesystem::create_directories(dir);

            nlohmann::json j = theme;
            std::ofstream file(themeFilePath(dir, fileName));
            if (!file.is_open())
                return false;
            file << j.dump(2);
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    bool EditorThemeStorage::remove(const std::string& dir, const std::string& name)
    {
        std::string fileName = sanitizeName(name);
        if (fileName.empty())
            return false;

        std::error_code ec;
        return std::filesystem::remove(themeFilePath(dir, fileName), ec);
    }

    std::string EditorThemeStorage::sanitizeName(const std::string& name)
    {
        std::string result;
        result.reserve(name.size());
        for (char c : name)
        {
            if (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_')
                result += c;
        }

        size_t first = result.find_first_not_of(' ');
        size_t last = result.find_last_not_of(' ');
        if (first == std::string::npos)
            return "";
        result = result.substr(first, last - first + 1);

        if (result == "Dark" || result == "Light")
            return "";
        return result;
    }

    std::string EditorThemeStorage::defaultThemesDirectory()
    {
        char* home = nullptr;
        size_t len = 0;
        _dupenv_s(&home, &len, "USERPROFILE");
        if (!home)
            _dupenv_s(&home, &len, "HOME");
        if (!home)
            return "";
        std::string result = std::string(home) + "/.vertexforge/themes";
        free(home);
        return result;
    }
}
