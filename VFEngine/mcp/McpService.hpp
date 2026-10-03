#pragma once

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/McpServer.hpp"
#include "protocol/PromptRegistry.hpp"
#include "protocol/ResourceRegistry.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/PluginToolBridge.hpp"
#include "transport/EventStreamHub.hpp"
#include "transport/HttpServer.hpp"
#include "transport/SecurityPolicy.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace mcp
{
    struct McpStatus
    {
        enum class State
        {
            Off,
            Listening,
            Error
        };

        State state = State::Off;
        uint16_t port = 0;
        std::string error;
        std::string clientName;
        std::string lastTool;
        uint64_t toolCalls = 0;
        uint64_t toolErrors = 0;
        std::size_t connections = 0;
        std::size_t toolCount = 0;
        std::size_t eventStreams = 0;  // open GET /mcp SSE streams (VK-1652)
    };

    // MCP Streamable HTTP endpoint (VK-1650): POST /mcp carries one JSON-RPC
    // message and gets one JSON response. GET /mcp (VK-1652) opens the standalone
    // SSE stream used for server -> client notifications (tools/list_changed).
    // Owns the tool/resource/prompt registries, protocol server, main-thread
    // queue, SSE hub, plugin tool bridge and the loopback HTTP listener.
    //
    // Lifetime: tools/resources/prompts are registered before start(). drain()
    // must be pumped from the editor main thread every frame while running.
    class McpService
    {
    public:
        static constexpr const char* endpointPath = "/mcp";

        explicit McpService(McpServer::Info info = {});
        ~McpService();

        McpService(const McpService&) = delete;
        McpService& operator=(const McpService&) = delete;

        ToolRegistry& registry() { return tools; }
        ResourceRegistry& resourceRegistry() { return resources; }
        PromptRegistry& promptRegistry() { return prompts; }
        MainThreadQueue& mainThreadQueue() { return queue; }

        // Main thread, once, after the plugins are up: subscribes the plugin tool
        // bridge. start() syncs the plugin tools before listening; drain() re-syncs
        // when a plugin changed them and broadcasts notifications/tools/list_changed.
        void enablePluginTools();

        // Sends a JSON-RPC notification to every open SSE stream (dropped when none).
        void broadcastNotification(const std::string& jsonText);

        // port 0 = ephemeral. token empty = no Authorization required.
        bool start(uint16_t port, const std::string& token);
        void stop();

        // Main thread, once per frame.
        void drain(std::chrono::milliseconds budget = std::chrono::milliseconds(8));

        bool isRunning() const { return http.isRunning(); }
        McpStatus status() const;

        // Exposed for tests: routes one HTTP request exactly as the listener does.
        http::HttpResponse handleHttp(const http::HttpRequest& request);

    private:
        ToolRegistry tools;
        ResourceRegistry resources;
        PromptRegistry prompts;
        MainThreadQueue queue;
        McpServer server;
        http::EventStreamHub hub;
        tools::PluginToolBridge pluginTools;
        bool pluginToolsEnabled = false;
        http::HttpServer http;
        http::SecurityPolicy security;

        mutable std::mutex statusMutex;
        std::string lastError;
        std::string sessionId;
    };
}
