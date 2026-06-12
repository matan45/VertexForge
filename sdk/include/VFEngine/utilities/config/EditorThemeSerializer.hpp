#pragma once
#include "EditorTheme.hpp"
#include <nlohmann/json.hpp>

namespace config
{
    inline void to_json(nlohmann::json& j, const EditorTheme& theme)
    {
        nlohmann::json colors = nlohmann::json::object();
        for (const auto& [name, color] : theme.colors)
            colors[name] = nlohmann::json::array({color.x, color.y, color.z, color.w});

        j = nlohmann::json{
            {"schemaVersion", nlohmann::json{{"major", EditorThemeSchemaVersion::major}, {"minor", EditorThemeSchemaVersion::minor}}},
            {"name", theme.name},
            {"basedOn", theme.basedOn},
            {"colors", colors}
        };
    }

    inline void from_json(const nlohmann::json& j, EditorTheme& theme)
    {
        EditorTheme defaults;
        theme.name = j.value("name", defaults.name);
        theme.basedOn = j.value("basedOn", defaults.basedOn);
        if (theme.basedOn != "Dark" && theme.basedOn != "Light")
            theme.basedOn = defaults.basedOn;

        theme.colors.clear();
        if (j.contains("colors") && j["colors"].is_object())
        {
            for (const auto& [name, value] : j["colors"].items())
            {
                if (!value.is_array() || value.size() < 4)
                    continue;
                if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number() || !value[3].is_number())
                    continue;
                theme.colors[name] = glm::vec4(
                    value[0].get<float>(),
                    value[1].get<float>(),
                    value[2].get<float>(),
                    value[3].get<float>()
                );
            }
        }
    }
}
