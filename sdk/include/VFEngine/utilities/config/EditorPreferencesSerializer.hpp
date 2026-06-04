#pragma once
#include "EditorPreferences.hpp"
#include <nlohmann/json.hpp>

namespace config
{
    using json = nlohmann::json;

    // ============================================
    // GeneralSettings
    // ============================================

    inline void to_json(json& j, const GeneralSettings& s)
    {
        j = json{
            {"projectName", s.projectName},
            {"defaultScene", s.defaultScene},
            {"autoSaveEnabled", s.autoSaveEnabled},
            {"autoSaveIntervalMinutes", s.autoSaveIntervalMinutes},
            {"loadLastProject", s.loadLastProject},
            {"showSplashScreen", s.showSplashScreen}
        };
    }

    inline void from_json(const json& j, GeneralSettings& s)
    {
        GeneralSettings defaults;
        s.projectName = j.value("projectName", defaults.projectName);
        s.defaultScene = j.value("defaultScene", defaults.defaultScene);
        s.autoSaveEnabled = j.value("autoSaveEnabled", defaults.autoSaveEnabled);
        s.autoSaveIntervalMinutes = j.value("autoSaveIntervalMinutes", defaults.autoSaveIntervalMinutes);
        s.loadLastProject = j.value("loadLastProject", defaults.loadLastProject);
        s.showSplashScreen = j.value("showSplashScreen", defaults.showSplashScreen);
    }

    // ============================================
    // AppearanceSettings
    // ============================================

    inline void to_json(json& j, const AppearanceSettings& s)
    {
        j = json{
            {"theme", s.theme},
            {"accentColor", json::array({s.accentColor.x, s.accentColor.y, s.accentColor.z, s.accentColor.w})},
            {"uiScale", s.uiScale},
            {"fontSize", s.fontSize},
            {"roundedCorners", s.roundedCorners},
            {"panelShadows", s.panelShadows},
            {"compactMode", s.compactMode}
        };
    }

    inline void from_json(const json& j, AppearanceSettings& s)
    {
        AppearanceSettings defaults;
        s.theme = j.value("theme", defaults.theme);
        if (j.contains("accentColor") && j["accentColor"].is_array() && j["accentColor"].size() >= 4)
        {
            s.accentColor = glm::vec4(
                j["accentColor"][0].get<float>(),
                j["accentColor"][1].get<float>(),
                j["accentColor"][2].get<float>(),
                j["accentColor"][3].get<float>()
            );
        }
        s.uiScale = j.value("uiScale", defaults.uiScale);
        s.fontSize = j.value("fontSize", defaults.fontSize);
        s.roundedCorners = j.value("roundedCorners", defaults.roundedCorners);
        s.panelShadows = j.value("panelShadows", defaults.panelShadows);
        s.compactMode = j.value("compactMode", defaults.compactMode);
    }

    // ============================================
    // InputSettings
    // ============================================

    inline void to_json(json& j, const InputSettings& s)
    {
        j = json{
            {"cameraSpeed", s.cameraSpeed},
            {"cameraSensitivity", s.cameraSensitivity},
            {"orbitMode", s.orbitMode},
            {"invertYAxis", s.invertYAxis},
            {"smoothCamera", s.smoothCamera},
            {"focusOnSelection", s.focusOnSelection}
        };
    }

    inline void from_json(const json& j, InputSettings& s)
    {
        InputSettings defaults;
        s.cameraSpeed = j.value("cameraSpeed", defaults.cameraSpeed);
        s.cameraSensitivity = j.value("cameraSensitivity", defaults.cameraSensitivity);
        s.orbitMode = j.value("orbitMode", defaults.orbitMode);
        s.invertYAxis = j.value("invertYAxis", defaults.invertYAxis);
        s.smoothCamera = j.value("smoothCamera", defaults.smoothCamera);
        s.focusOnSelection = j.value("focusOnSelection", defaults.focusOnSelection);
    }

    // ============================================
    // RenderingSettings
    // ============================================

    inline void to_json(json& j, const RenderingSettings& s)
    {
        j = json{
            {"vsync", s.vsync},
            {"hdrMode", s.hdrMode},
            {"msaaSamples", s.msaaSamples},
            {"shadowQuality", s.shadowQuality},
            {"shadowDistance", s.shadowDistance},
            {"meshShaders", s.meshShaders},
            {"bindlessTextures", s.bindlessTextures},
            {"rayTracing", s.rayTracing}
        };
    }

    inline void from_json(const json& j, RenderingSettings& s)
    {
        RenderingSettings defaults;
        s.vsync = j.value("vsync", defaults.vsync);
        s.hdrMode = j.value("hdrMode", defaults.hdrMode);
        s.msaaSamples = j.value("msaaSamples", defaults.msaaSamples);
        s.shadowQuality = j.value("shadowQuality", defaults.shadowQuality);
        s.shadowDistance = j.value("shadowDistance", defaults.shadowDistance);
        s.meshShaders = j.value("meshShaders", defaults.meshShaders);
        s.bindlessTextures = j.value("bindlessTextures", defaults.bindlessTextures);
        s.rayTracing = j.value("rayTracing", defaults.rayTracing);
    }

    // ============================================
    // EditorSettings
    // ============================================

    inline void to_json(json& j, const EditorSettings& s)
    {
        j = json{
            {"gridSize", s.gridSize},
            {"gizmoSize", s.gizmoSize},
            {"snapTranslate", s.snapTranslate},
            {"snapRotate", s.snapRotate},
            {"snapScale", s.snapScale},
            {"showGrid", s.showGrid},
            {"highlightSelection", s.highlightSelection},
            {"enableGizmos", s.enableGizmos}
        };
    }

    inline void from_json(const json& j, EditorSettings& s)
    {
        EditorSettings defaults;
        s.gridSize = j.value("gridSize", defaults.gridSize);
        s.gizmoSize = j.value("gizmoSize", defaults.gizmoSize);
        s.snapTranslate = j.value("snapTranslate", defaults.snapTranslate);
        s.snapRotate = j.value("snapRotate", defaults.snapRotate);
        s.snapScale = j.value("snapScale", defaults.snapScale);
        s.showGrid = j.value("showGrid", defaults.showGrid);
        s.highlightSelection = j.value("highlightSelection", defaults.highlightSelection);
        s.enableGizmos = j.value("enableGizmos", defaults.enableGizmos);
    }

    // ============================================
    // DebugSettings
    // ============================================

    inline void to_json(json& j, const DebugSettings& s)
    {
        j = json{
            {"showFPS", s.showFPS},
            {"showGPUTime", s.showGPUTime},
            {"showDrawCalls", s.showDrawCalls},
            {"logLevel", s.logLevel},
            {"vulkanValidation", s.vulkanValidation},
            {"gpuCrashDebugging", s.gpuCrashDebugging},
            {"showShadowCascades", s.showShadowCascades},
            {"showOverdraw", s.showOverdraw}
        };
    }

    inline void from_json(const json& j, DebugSettings& s)
    {
        DebugSettings defaults;
        s.showFPS = j.value("showFPS", defaults.showFPS);
        s.showGPUTime = j.value("showGPUTime", defaults.showGPUTime);
        s.showDrawCalls = j.value("showDrawCalls", defaults.showDrawCalls);
        s.logLevel = j.value("logLevel", defaults.logLevel);
        s.vulkanValidation = j.value("vulkanValidation", defaults.vulkanValidation);
        s.gpuCrashDebugging = j.value("gpuCrashDebugging", defaults.gpuCrashDebugging);
        s.showShadowCascades = j.value("showShadowCascades", defaults.showShadowCascades);
        s.showOverdraw = j.value("showOverdraw", defaults.showOverdraw);
    }

    // ============================================
    // EditorPreferences (master struct)
    // ============================================

    inline void to_json(json& j, const EditorPreferences& prefs)
    {
        j = json{
            {"schemaVersion", json{{"major", EditorSettingsSchemaVersion::major}, {"minor", EditorSettingsSchemaVersion::minor}}},
            {"general", prefs.general},
            {"appearance", prefs.appearance},
            {"input", prefs.input},
            {"rendering", prefs.rendering},
            {"editor", prefs.editor},
            {"debug", prefs.debug}
        };
    }

    inline void from_json(const json& j, EditorPreferences& prefs)
    {
        if (j.contains("general") && j["general"].is_object())
            prefs.general = j["general"].get<GeneralSettings>();

        if (j.contains("appearance") && j["appearance"].is_object())
            prefs.appearance = j["appearance"].get<AppearanceSettings>();

        if (j.contains("input") && j["input"].is_object())
            prefs.input = j["input"].get<InputSettings>();

        if (j.contains("rendering") && j["rendering"].is_object())
            prefs.rendering = j["rendering"].get<RenderingSettings>();

        if (j.contains("editor") && j["editor"].is_object())
            prefs.editor = j["editor"].get<EditorSettings>();

        if (j.contains("debug") && j["debug"].is_object())
            prefs.debug = j["debug"].get<DebugSettings>();
    }
}
