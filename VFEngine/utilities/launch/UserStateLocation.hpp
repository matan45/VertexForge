#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// VK-1732: where the editor keeps the state a user makes while working (logs, crash reports, the window
// layout, plugin config data, Plugin Manager choices, plugins the user scaffolded).
//
// An installed engine lives in %LOCALAPPDATA%\Programs\VertexForge, and the VK-1693 MSI deletes that
// whole folder on every upgrade and uninstall. So the release package writes
//
//   "stateLocation": "user"
//
// into the engine.json beside Editor.exe (tools/package_release.lua), and the editor then keeps its
// state in %USERPROFILE%\.vertexforge\editor (the folder the launcher and the editor's settings already
// use) and the user's plugins in Documents\VertexForge\Plugins. The dev tree's engine.json says
// "engine" (tools/engine_version.lua): everything stays where it always was, beside the engine.
// No engine.json (the Runtime, Tests.exe) or one without the member means "engine" too.
//
// This header carries no Win32 types: it is exported into the plugin SDK.
namespace launch
{
    inline constexpr std::string_view stateLocationManifestKey = "stateLocation";
    inline constexpr std::string_view stateLocationEngine = "engine";
    inline constexpr std::string_view stateLocationUser = "user";

    inline constexpr std::wstring_view engineManifestFileName = L"engine.json";
    // %USERPROFILE%\.vertexforge, the launcher's configDir; the editor's own files go in its "editor" folder,
    // apart from the launcher's per-launch logs/ (which the launcher prunes).
    inline constexpr std::wstring_view profileStateDirName = L".vertexforge";
    inline constexpr std::wstring_view userEditorStateDirName = L"editor";
    // Documents\VertexForge\Plugins
    inline constexpr std::wstring_view userPluginsParentDirName = L"VertexForge";
    inline constexpr std::wstring_view userPluginsDirName = L"Plugins";

    enum class StateLocation
    {
        Engine,  // beside the engine, as before VK-1732 (the dev tree)
        User     // the per-user folders (an installed engine)
    };

    struct UserStateInputs
    {
        // The text of <exeDir>/engine.json; nullopt when there is none.
        std::optional<std::string> engineManifestText;
        // engine.json exists but could not be read.
        bool engineManifestUnreadable = false;
        std::filesystem::path profileDir;    // USERPROFILE, else HOME; empty when unknown
        std::filesystem::path documentsDir;  // the Documents known folder; empty when unknown
        std::filesystem::path exeDir;        // the folder of the process's executable
        std::filesystem::path workingDir;    // the current directory
    };

    // Every path is absolute when the inputs are. An empty path means "keep the behaviour from before
    // VK-1732" (the caller's own default).
    struct UserStateLocation
    {
        StateLocation mode = StateLocation::Engine;
        std::filesystem::path stateRoot;            // User: <profile>/.vertexforge/editor          Engine: empty
        std::filesystem::path logsDir;              // User: <root>/logs                            Engine: <cwd>/logs
        std::filesystem::path crashesDir;           // User: <root>/crashes                         Engine: <cwd>/crashes
        std::filesystem::path imguiIniFile;         // User: <root>/imgui.ini                       Engine: <exeDir>/imgui.ini
        std::filesystem::path pluginDataDir;        // User: <root>/plugin-data                     Engine: empty
        std::filesystem::path pluginOverridesFile;  // User: <root>/plugin-overrides.json           Engine: empty
        std::filesystem::path savesFallbackDir;     // User: <root>/saves                           Engine: empty
        std::filesystem::path diagnosticsDir;       // User: <root>/diagnostics                     Engine: empty
        std::filesystem::path userPluginsDir;       // User: <Documents>/VertexForge/Plugins        Engine: empty
        // The engine's root folder, which holds sdk/ and plugins/: four levels above the exe folder in the
        // dev tree (bin/Editor/<Config>/x64) and in the release bundle alike.
        std::filesystem::path engineRoot;
        // What went wrong while deciding (an unreadable engine.json, an unknown value, no profile folder),
        // for the log: the resolver runs before the log file exists.
        std::vector<std::string> problems;

        bool isUser() const noexcept { return mode == StateLocation::User; }
    };

    // Pure: decides from the inputs alone. Never throws for bad manifest text.
    UserStateLocation resolveUserStateLocation(const UserStateInputs& inputs);

    // The answer for this process, worked out on first use from the executable's own engine.json, the
    // environment and the current directory, then kept. A static library's statics exist once per binary,
    // so every module computes its own copy, from the same process-wide inputs, to the same answer. Never
    // throws: anything unexpected gives Engine mode with a problem.
    const UserStateLocation& processUserState() noexcept;

    // User mode: creates the state root, logs/ and crashes/. Returns a problem per folder it could not
    // create. Engine mode: does nothing.
    std::vector<std::string> createUserStateFolders(const UserStateLocation& location);
}
