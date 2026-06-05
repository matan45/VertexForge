#pragma once
#include <string>
#include <cstdint>
#include <glm/glm.hpp>

namespace config
{
    struct EditorSettingsSchemaVersion
    {
        static constexpr uint32_t major = 1;
        static constexpr uint32_t minor = 0;
    };

    struct GeneralSettings
    {
        std::string projectName;
        std::string defaultScene;
        bool autoSaveEnabled = true;
        int autoSaveIntervalMinutes = 5;
        bool loadLastProject = true;
        bool showSplashScreen = false;
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

    struct InputSettings
    {
        float cameraSpeed = 5.0f;
        float cameraSensitivity = 0.8f;
        std::string orbitMode = "Unreal";
        bool invertYAxis = false;
        bool smoothCamera = true;
        bool focusOnSelection = true;
    };

    struct RenderingSettings
    {
        bool vsync = true;
        std::string hdrMode = "Enabled";
        int msaaSamples = 4;
        std::string shadowQuality = "High";
        float shadowDistance = 200.0f;
        bool meshShaders = true;
        bool bindlessTextures = true;
        bool rayTracing = false;
    };

    struct EditorSettings
    {
        float gridSize = 1.0f;
        float gizmoSize = 1.2f;
        float snapTranslate = 1.0f;
        float snapRotate = 15.0f;
        float snapScale = 0.1f;
        bool showGrid = true;
        bool highlightSelection = true;
        bool enableGizmos = true;
    };

    struct DebugSettings
    {
        bool showFPS = true;
        bool showGPUTime = true;
        bool showDrawCalls = true;
        std::string logLevel = "Info";
        bool vulkanValidation = true;
        bool gpuCrashDebugging = false;
        bool showShadowCascades = false;
        bool showOverdraw = false;
    };

    struct EditorPreferences
    {
        GeneralSettings general;
        AppearanceSettings appearance;
        InputSettings input;
        RenderingSettings rendering;
        EditorSettings editor;
        DebugSettings debug;

        static EditorPreferences createDefault()
        {
            return EditorPreferences{};
        }
    };
}
