#pragma once
#include "McpService.hpp"
#include "config/EditorPreferences.hpp"
#include "events/EventDispatcher.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace editor
{
    class EditorCamera;

    // MCP flags parsed from the editor command line (Main.cpp). They override the
    // saved preference for this session only and are never written back.
    struct McpLaunchOptions
    {
        bool enable = false;               // --mcp: enable with the preference (or default) port
        std::optional<uint16_t> port;      // --mcp-port <n>: forces enabled
        std::optional<std::string> token;  // --mcp-token <t>
    };

    // Editor-side owner of the MCP server (VK-1650). Lives in Editor.exe because the
    // EventDispatcher is per-binary: only this binary sees the real event handlers.
    //
    // Threading: every member function is main-thread only. Tool work reaches the main
    // thread through drain(), which EditorHandler pumps once per frame after the frame
    // task graph has joined.
    class EditorMcpHost
    {
    public:
        static constexpr uint16_t minPort = 1024;
        static constexpr uint16_t maxPort = 65535;

        EditorMcpHost();
        ~EditorMcpHost();

        EditorMcpHost(const EditorMcpHost&) = delete;
        EditorMcpHost& operator=(const EditorMcpHost&) = delete;

        // Registers the core tools, then starts the server if the preference or the
        // command line enables it. Call once, after services and plugins are up.
        void init(const McpLaunchOptions& launchOptions);

        // The viewport camera for the camera_* tools (VK-1651). Set before init(); the
        // tools are only registered when it is non-null. Not owned: the ViewPort window
        // owns it and outlives the server (shutdown() runs before UI teardown).
        void setEditorCamera(EditorCamera* camera) { editorCamera = camera; }

        // Runs queued tool work on the main thread (8 ms budget) and applies a pending
        // restart requested by a preference change.
        void drain();

        // Stops the server; pending tool calls fail with "shutting down". Must run
        // before any service the tools dispatch to is torn down.
        void shutdown();

        // Live state for the status bar and the preferences window. While stopped,
        // `port` is the configured port, and a configuration problem (bad port) is
        // reported as State::Error.
        mcp::McpStatus status() const;

        // True while the command-line flags are in effect (until the user applies a
        // change to the MCP preferences).
        bool isCommandLineOverride() const { return commandLineOverride; }

        // "7878" -> 7878. Empty optional for non-numeric or out-of-range [minPort, maxPort].
        static std::optional<uint16_t> parsePort(std::string_view text);

    private:
        struct Endpoint
        {
            bool enabled = false;
            int port = 0;
            std::string token;

            bool operator==(const Endpoint&) const = default;
        };

        static Endpoint fromSettings(const config::McpSettings& settings);
        static std::string buildInstructions();

        void onSettingsChanged(const config::McpSettings& settings);
        void applyEndpoint(const Endpoint& endpoint);

        std::unique_ptr<mcp::McpService> service;
        EditorCamera* editorCamera = nullptr;
        Endpoint active;
        // Restarts are deferred to drain(): a settings change may be published from
        // inside a tool call that drain() is running, and stopping the server there
        // would join the connection thread that is waiting on that very call.
        std::optional<Endpoint> pending;
        config::McpSettings lastPreference;
        bool commandLineOverride = false;
        std::string configError;

        events::ScopedSubscription settingsSubscription;
    };
}
