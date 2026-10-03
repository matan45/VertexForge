#include "EditorMcpHost.hpp"
#include "EditorCameraTools.hpp"
#include "tools/CoreTools.hpp"
#include "events/editor/EditorSettingsEvents.hpp"
#include "Import.hpp"
#include "print/Log.hpp"
#include <charconv>
#include <exception>
#include <string>
#include <system_error>

namespace editor
{
    EditorMcpHost::EditorMcpHost() = default;

    EditorMcpHost::~EditorMcpHost()
    {
        shutdown();
    }

    std::optional<uint16_t> EditorMcpHost::parsePort(std::string_view text)
    {
        if (text.empty())
            return std::nullopt;

        int value = 0;
        const char* last = text.data() + text.size();
        auto [ptr, ec] = std::from_chars(text.data(), last, value);
        if (ec != std::errc{} || ptr != last)
            return std::nullopt;
        if (value < minPort || value > maxPort)
            return std::nullopt;
        return static_cast<uint16_t>(value);
    }

    EditorMcpHost::Endpoint EditorMcpHost::fromSettings(const config::McpSettings& settings)
    {
        return Endpoint{settings.enabled, settings.port, settings.token};
    }

    std::string EditorMcpHost::buildInstructions()
    {
        return
            "VertexForge Editor control server. Tools act on the live editor session.\n"
            "Typical workflow: editor_get_info -> scene_get_hierarchy -> entity_create / "
            "component_add / material_create to build the scene -> script_write (.mt files under "
            "scripts/game) -> scripts_build (fix any reported errors, then build again) -> "
            "script_attach -> play_start -> logs_read -> play_stop -> scene_save.\n"
            "Conventions: entity ids are integers; vectors are [x, y, z]; rotations are Euler "
            "angles in degrees; paths are relative to the project directory unless a tool says "
            "otherwise. Always play_stop before editing the scene again, and scene_save to persist.";
    }

    void EditorMcpHost::init(const McpLaunchOptions& launchOptions)
    {
        if (service)
            return;

        mcp::McpServer::Info info;
        info.instructions = buildInstructions();
        service = std::make_unique<mcp::McpService>(std::move(info));
        mcp::tools::registerCoreTools(service->registry(), service->mainThreadQueue());
        if (editorCamera)
            registerEditorCameraTools(service->registry(), editorCamera);

        auto& dispatcher = events::EventDispatcher::instance();
        lastPreference = dispatcher.query(events::editor::GetEditorSettingsQuery{}).mcp;

        Endpoint endpoint = fromSettings(lastPreference);
        if (launchOptions.enable || launchOptions.port || launchOptions.token)
        {
            commandLineOverride = true;
            // --mcp-token alone only replaces the token; --mcp / --mcp-port enable the server.
            if (launchOptions.enable || launchOptions.port)
                endpoint.enabled = true;
            if (launchOptions.port)
                endpoint.port = *launchOptions.port;
            if (launchOptions.token)
                endpoint.token = *launchOptions.token;
        }

        settingsSubscription = events::ScopedSubscription(
            dispatcher.subscribe<events::editor::EditorSettingsChangedNotification>(
                [this](const events::editor::EditorSettingsChangedNotification& n)
                {
                    onSettingsChanged(n.settings.mcp);
                }));

        // Not inside drain(), so starting synchronously is safe here.
        applyEndpoint(endpoint);
    }

    void EditorMcpHost::onSettingsChanged(const config::McpSettings& settings)
    {
        // The notification fires for every preference change (theme, log level, ...).
        // Only a change to the MCP section restarts the server or ends a command-line override.
        const Endpoint preference = fromSettings(settings);
        if (preference == fromSettings(lastPreference))
            return;

        lastPreference = settings;
        commandLineOverride = false;
        pending = preference;
    }

    void EditorMcpHost::drain()
    {
        if (!service)
            return;

        if (pending)
        {
            Endpoint endpoint = std::move(*pending);
            pending.reset();
            applyEndpoint(endpoint);
        }

        if (service->isRunning())
            service->drain();
    }

    void EditorMcpHost::applyEndpoint(const Endpoint& endpoint)
    {
        if (!service)
            return;

        const bool running = service->isRunning();
        if (running && endpoint == active)
            return;

        active = endpoint;
        configError.clear();

        if (!endpoint.enabled)
        {
            service->stop();
            return;
        }

        if (endpoint.port < minPort || endpoint.port > maxPort)
        {
            service->stop();
            configError = "Port " + std::to_string(endpoint.port) + " is outside " +
                          std::to_string(minPort) + "-" + std::to_string(maxPort);
            vfLogWarning("[MCP] not started: {}", configError);
            return;
        }

        try
        {
            // start() stops a running server first and reports bind failures via status().
            service->start(static_cast<uint16_t>(endpoint.port), endpoint.token);
        }
        catch (const std::exception& e)
        {
            configError = e.what();
            vfLogError("[MCP] failed to start: {}", configError);
        }
    }

    void EditorMcpHost::shutdown()
    {
        settingsSubscription.unsubscribe();
        pending.reset();
        if (service)
        {
            // An assets_import runs on a connection thread that stop() joins; ask
            // the import pipeline to bail out so editor exit is not held hostage by
            // a long agent import. (The editor is exiting, so cancelling a user
            // import too is harmless - cleanUp() shuts Import down right after.)
            controllers::Import::requestCancel();
            service->stop();
            service.reset();
        }
    }

    mcp::McpStatus EditorMcpHost::status() const
    {
        mcp::McpStatus result;
        if (service)
            result = service->status();

        if (result.state != mcp::McpStatus::State::Listening)
        {
            result.port = static_cast<uint16_t>(
                (active.port >= 0 && active.port <= maxPort) ? active.port : 0);

            // A disabled server is Off even if an earlier start attempt failed.
            if (!active.enabled)
            {
                result.state = mcp::McpStatus::State::Off;
                result.error.clear();
            }
            else if (!configError.empty())
            {
                result.state = mcp::McpStatus::State::Error;
                result.error = configError;
            }
        }
        return result;
    }
}
