#pragma once
#include "EditorPreferences.hpp"
#include <nlohmann/json.hpp>

namespace config
{
    using json = nlohmann::json;

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
    // DebugSettings
    // ============================================

    inline void to_json(json& j, const DebugSettings& s)
    {
        j = json{
            {"showFPS", s.showFPS},
            {"showGPUTime", s.showGPUTime},
            {"showDrawCalls", s.showDrawCalls},
            {"logLevel", s.logLevel}
        };
    }

    inline void from_json(const json& j, DebugSettings& s)
    {
        DebugSettings defaults;
        s.showFPS = j.value("showFPS", defaults.showFPS);
        s.showGPUTime = j.value("showGPUTime", defaults.showGPUTime);
        s.showDrawCalls = j.value("showDrawCalls", defaults.showDrawCalls);
        s.logLevel = j.value("logLevel", defaults.logLevel);
    }

    // ============================================
    // WindowLayoutSettings
    // ============================================

    inline void to_json(json& j, const WindowLayoutSettings& s)
    {
        j = json{
            {"startupLayout", s.startupLayout}
        };
    }

    inline void from_json(const json& j, WindowLayoutSettings& s)
    {
        WindowLayoutSettings defaults;
        s.startupLayout = j.value("startupLayout", defaults.startupLayout);
    }

    // ============================================
    // ExportSettings
    // ============================================

    inline void to_json(json& j, const ExportSettings& s)
    {
        j = json{
            {"lastOutputDirectory", s.lastOutputDirectory},
            {"cleanBuild", s.cleanBuild},
            {"verifyIntegrity", s.verifyIntegrity},
            {"buildScripts", s.buildScripts},
            {"stripUnreferencedAssets", s.stripUnreferencedAssets},
            {"alwaysIncludePatterns", s.alwaysIncludePatterns}
        };
    }

    inline void from_json(const json& j, ExportSettings& s)
    {
        ExportSettings defaults;
        s.lastOutputDirectory = j.value("lastOutputDirectory", defaults.lastOutputDirectory);
        s.cleanBuild = j.value("cleanBuild", defaults.cleanBuild);
        s.verifyIntegrity = j.value("verifyIntegrity", defaults.verifyIntegrity);
        s.buildScripts = j.value("buildScripts", defaults.buildScripts);
        s.stripUnreferencedAssets = j.value("stripUnreferencedAssets", defaults.stripUnreferencedAssets);
        if (j.contains("alwaysIncludePatterns") && j["alwaysIncludePatterns"].is_array())
        {
            s.alwaysIncludePatterns.clear();
            for (const auto& pattern : j["alwaysIncludePatterns"])
            {
                if (pattern.is_string())
                    s.alwaysIncludePatterns.push_back(pattern.get<std::string>());
            }
        }
    }

    // ============================================
    // EditorPreferences (master struct)
    // ============================================

    inline void to_json(json& j, const EditorPreferences& prefs)
    {
        j = json{
            {"schemaVersion", json{{"major", EditorSettingsSchemaVersion::major}, {"minor", EditorSettingsSchemaVersion::minor}}},
            {"appearance", prefs.appearance},
            {"debug", prefs.debug},
            {"windowLayout", prefs.windowLayout},
            {"export", prefs.exportSettings}
        };
    }

    inline void from_json(const json& j, EditorPreferences& prefs)
    {
        if (j.contains("appearance") && j["appearance"].is_object())
            prefs.appearance = j["appearance"].get<AppearanceSettings>();

        if (j.contains("debug") && j["debug"].is_object())
            prefs.debug = j["debug"].get<DebugSettings>();

        if (j.contains("windowLayout") && j["windowLayout"].is_object())
            prefs.windowLayout = j["windowLayout"].get<WindowLayoutSettings>();

        if (j.contains("export") && j["export"].is_object())
            prefs.exportSettings = j["export"].get<ExportSettings>();
    }
}
