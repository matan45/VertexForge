#pragma once

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/McpServer.hpp"
#include "protocol/ToolRegistry.hpp"
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
    };

    // MCP Streamable HTTP endpoint (VK-1650): POST /mcp carries one JSON-RPC
    // message and gets one JSON response (no SSE stream). Owns the tool registry,
    // protocol server, main-thread queue and the loopback HTTP listener.
    //
    // Lifetime: registerTools happens through registry() before start(). drain()
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
        MainThreadQueue& mainThreadQueue() { return queue; }

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
        MainThreadQueue queue;
        McpServer server;
        http::HttpServer http;
        http::SecurityPolicy security;

        mutable std::mutex statusMutex;
        std::string lastError;
        std::string sessionId;
    };
}
