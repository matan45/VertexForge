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
        static constexpr uint32_t minor = 4;
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
        // Separate readouts: showGPUTime is a timestamp-derived GPU span, showCPUTime
        // is the wall-clock frame time. Until VK-1529 the "GPU" toggle in fact showed
        // CPU time, so the two are split rather than one relabelled.
        bool showCPUTime = true;
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

    struct MemorySettings
    {
        // Default = 8 GiB.  0 = advisory/disabled (gate always admits).
        uint64_t cpuMemoryBudgetBytes = 8ull * 1024 * 1024 * 1024;
    };

    // Per-user editor-wide audio controls. This is intentionally separate from
    // project audio settings because it also affects previews and play testing.
    struct EditorAudioSettings
    {
        bool globalMuted = false;
    };

    // Undo/redo history limits. Deliberately separate from MemorySettings: that field is
    // the CPU allocation gate, this is editor behaviour.
    struct UndoSettings
    {
        // 0 = unlimited, matching MemorySettings' convention.
        uint32_t maxHistoryDepth = 50;

        // Ceiling across the undo AND redo stacks. 0 = unlimited. A depth cap alone cannot
        // bound RAM: terrain strokes snapshot whole tile arrays before and after, so one
        // entry ranges from a few KB (a file rename) to ~4 MB (a 129^2 4-tile paint stroke).
        uint64_t maxHistoryBytes = 512ull * 1024 * 1024;
    };

    struct EditorPreferences
    {
        AppearanceSettings appearance;
        DebugSettings debug;
        WindowLayoutSettings windowLayout;
        ExportSettings exportSettings;
        PreviewWindowSettings previewWindows;
        MemorySettings memory;
        EditorAudioSettings audio;
        UndoSettings undo;

        static EditorPreferences createDefault()
        {
            return EditorPreferences{};
        }
    };
}
