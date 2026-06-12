#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <glm/glm.hpp>

namespace config
{
    struct EditorSettingsSchemaVersion
    {
        static constexpr uint32_t major = 1;
        static constexpr uint32_t minor = 2;
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

    // Last-used options of the Export Game dialog (per-user, not per-project)
    struct ExportSettings
    {
        std::string lastOutputDirectory;
        bool cleanBuild = false;
        bool verifyIntegrity = true;
        bool buildScripts = true;
        bool stripUnreferencedAssets = false;
        std::vector<std::string> alwaysIncludePatterns;
    };

    // Last-used size per preview window type ("MeshPreview", "AudioPreview", ...),
    // used as the default for assets that have never been opened before.
    struct PreviewWindowSettings
    {
        std::map<std::string, glm::vec2> lastSizes;
    };

    struct EditorPreferences
    {
        AppearanceSettings appearance;
        DebugSettings debug;
        WindowLayoutSettings windowLayout;
        ExportSettings exportSettings;
        PreviewWindowSettings previewWindows;

        static EditorPreferences createDefault()
        {
            return EditorPreferences{};
        }
    };
}
