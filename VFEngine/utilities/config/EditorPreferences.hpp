#pragma once
#include <string>
#include <cstdint>
#include <glm/glm.hpp>

namespace config
{
    struct EditorSettingsSchemaVersion
    {
        static constexpr uint32_t major = 1;
        static constexpr uint32_t minor = 1;
    };

    struct AppearanceSettings
    {
        std::string theme = "Dark";
        glm::vec4 accentColor = {0.9f, 0.75f, 0.1f, 1.0f};
        float uiScale = 1.0f;
        int fontSize = 14;
        bool roundedCorners = true;
        bool panelShadows = true;
        bool compactMode = false;
    };

    struct DebugSettings
    {
        bool showFPS = true;
        bool showGPUTime = true;
        bool showDrawCalls = true;
        std::string logLevel = "Info";
    };

    struct WindowLayoutSettings
    {
        std::string startupLayout = "Default";
    };

    struct EditorPreferences
    {
        AppearanceSettings appearance;
        DebugSettings debug;
        WindowLayoutSettings windowLayout;

        static EditorPreferences createDefault()
        {
            return EditorPreferences{};
        }
    };
}
