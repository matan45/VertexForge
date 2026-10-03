#pragma once

#include "JsonRpc.hpp"
#include "ToolRegistry.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace mcp
{
    // Transport-agnostic MCP method dispatcher (lifecycle + tools).
    // Thread-safe: may be called concurrently from several connection threads.
    class McpServer
    {
    public:
        static constexpr const char* latestProtocolVersion = "2025-06-18";

        struct Info
        {
            std::string name = "vertexforge-editor";
            std::string title = "VertexForge Editor";
            std::string version = "1.0.0";
            std::string instructions;  // sent in the initialize result
        };

        // Runs a task on the editor main thread (MainThreadQueue::invoke). When
        // unset, Main-affinity tools run inline on the calling thread (tests).
        using MainThreadInvoker =
            std::function<nlohmann::json(std::function<nlohmann::json()>, std::chrono::milliseconds)>;

        McpServer(const ToolRegistry& tools, Info info);

        void setMainThreadInvoker(MainThreadInvoker invoker);

        // Returns the response to send, or nullopt for notifications / client
        // responses (HTTP 202 Accepted).
        std::optional<nlohmann::json> handle(const jsonrpc::Message& message);

        // parse + handle. Parse failures yield an error response.
        std::optional<nlohmann::json> handleBody(std::string_view body);

        // Observability for the editor status item.
        std::string clientName() const;
        std::string lastToolName() const;
        uint64_t toolCallCount() const { return toolCalls.load(std::memory_order_relaxed); }
        uint64_t toolErrorCount() const { return toolErrors.load(std::memory_order_relaxed); }

    private:
        nlohmann::json handleInitialize(const nlohmann::json& params);
        nlohmann::json handleToolsCall(const nlohmann::json& params);

        const ToolRegistry& tools;
        Info info;
        MainThreadInvoker invoker;

        mutable std::mutex statsMutex;
        std::string connectedClient;
        std::string lastTool;
        std::atomic<uint64_t> toolCalls{0};
        std::atomic<uint64_t> toolErrors{0};
    };
}
