#pragma once
#include <glm/vec4.hpp>
#include <cstdint>
#include <map>
#include <string>

namespace config
{
    struct EditorThemeSchemaVersion
    {
        static constexpr uint32_t major = 1;
        static constexpr uint32_t minor = 0;
    };

    // A named editor color theme. Colors are keyed by ImGui style color name
    // (ImGui::GetStyleColorName) so files stay valid across ImGui upgrades:
    // unknown names are ignored, missing names keep the built-in base color.
    struct EditorTheme
    {
        std::string name;
        std::string basedOn = "Dark"; // built-in base ("Dark"/"Light") applied before the color overlay
        std::map<std::string, glm::vec4> colors;
    };
}
