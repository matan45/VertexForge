#include "UIThemeSerialization.hpp"
#include "../print/Log.hpp"
#include "../resource/VFSHelpers.hpp"
#include <fstream>

namespace utilities::ui
{
    using json = nlohmann::json;

    namespace
    {
        json writeColor(const glm::vec4& c)
        {
            return json::array({c.r, c.g, c.b, c.a});
        }

        glm::vec4 readColor(const json& j)
        {
            glm::vec4 c{1.0f};
            if (j.is_array() && j.size() >= 4)
            {
                c = {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>()};
            }
            return c;
        }

        asset::AssetRef readAssetValue(const std::string& value)
        {
            // Hex GUID is the canonical on-disk form; a project-relative path
            // is accepted for hand-authored themes.
            asset::AssetRef ref = asset::AssetRef::fromHexString(value);
            if (!ref.isValid())
            {
                ref = asset::AssetRef::fromPath(value);
            }
            return ref;
        }
    }

    json UIThemeSerialization::toJson(const UITheme& theme)
    {
        json stylesJson = json::object();
        for (const auto& [key, style] : theme.styles)
        {
            json s;
            if (!style.colors.empty())
            {
                json colors = json::object();
                for (const auto& [name, color] : style.colors)
                {
                    colors[name] = writeColor(color);
                }
                s["colors"] = colors;
            }
            if (!style.floats.empty())
            {
                json floats = json::object();
                for (const auto& [name, value] : style.floats)
                {
                    floats[name] = value;
                }
                s["floats"] = floats;
            }
            if (!style.assets.empty())
            {
                json assets = json::object();
                for (const auto& [name, ref] : style.assets)
                {
                    if (ref.isValid())
                    {
                        assets[name] = ref.toHexString();
                    }
                }
                s["assets"] = assets;
            }
            stylesJson[key] = s;
        }

        json j;
        j["version"] = 1;
        j["styles"] = stylesJson;
        return j;
    }

    UITheme UIThemeSerialization::fromJson(const json& j)
    {
        UITheme theme;
        if (!j.contains("styles") || !j["styles"].is_object())
        {
            return theme;
        }

        for (const auto& [key, s] : j["styles"].items())
        {
            UIThemeStyle style;
            if (s.contains("colors") && s["colors"].is_object())
            {
                for (const auto& [name, value] : s["colors"].items())
                {
                    style.colors[name] = readColor(value);
                }
            }
            if (s.contains("floats") && s["floats"].is_object())
            {
                for (const auto& [name, value] : s["floats"].items())
                {
                    if (value.is_number())
                    {
                        style.floats[name] = value.get<float>();
                    }
                }
            }
            if (s.contains("assets") && s["assets"].is_object())
            {
                for (const auto& [name, value] : s["assets"].items())
                {
                    if (value.is_string())
                    {
                        asset::AssetRef ref = readAssetValue(value.get<std::string>());
                        if (ref.isValid())
                        {
                            style.assets[name] = ref;
                        }
                    }
                }
            }
            theme.styles[key] = std::move(style);
        }
        return theme;
    }

    bool UIThemeSerialization::saveToFile(const UITheme& theme, const std::string& filePath)
    {
        try
        {
            std::ofstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to open theme file for writing: {}", filePath);
                return false;
            }
            file << toJson(theme).dump(2);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save theme {}: {}", filePath, e.what());
            return false;
        }
    }

    std::optional<UITheme> UIThemeSerialization::loadFromFile(const std::string& filePath)
    {
        try
        {
            json j = resource::readJsonFile(filePath);
            if (j.is_null())
            {
                return std::nullopt;
            }
            return fromJson(j);
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to load theme {}: {}", filePath, e.what());
            return std::nullopt;
        }
    }
}
