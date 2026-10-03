#pragma once

#include <nlohmann/json.hpp>
#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mcp
{
    // Where a tool handler runs. Main = marshalled onto the editor main thread
    // (anything touching EventDispatcher handlers / EnTT / ImGui state). Worker =
    // runs directly on the HTTP connection thread (pure filesystem work, long
    // imports); such tools must hop to the main thread themselves for any
    // dispatcher call.
    enum class ThreadAffinity
    {
        Main,
        Worker
    };

    struct ToolResult
    {
        nlohmann::json structured;  // returned as structuredContent (wrapped in {"result":..} when not an object)
        std::string text;           // human-readable text; defaults to structured.dump(2)
        bool isError = false;
        // Content blocks emitted after the text block (MCP 2025-06-18), e.g. images.
        std::vector<nlohmann::json> extraContent;

        static ToolResult ok(nlohmann::json structured, std::string text = {});
        static ToolResult error(std::string message);
        // One {type:"image", data, mimeType} block; `base64` is the encoded image bytes.
        static ToolResult image(std::string base64, std::string mimeType, nlohmann::json structured,
                                std::string text = {});

        nlohmann::json toJson() const;
    };

    using ToolHandler = std::function<ToolResult(const nlohmann::json& args)>;

    struct ToolDef
    {
        std::string name;
        std::string title;
        std::string description;
        nlohmann::json inputSchema = nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}};
        ToolHandler handler;
        ThreadAffinity affinity = ThreadAffinity::Main;
        std::chrono::milliseconds timeout{10000};
        bool readOnly = false;     // annotations.readOnlyHint
        bool destructive = false;  // annotations.destructiveHint
    };

    // Immutable once the server starts: tools are registered on the main thread
    // before HttpServer::start, then only read from connection threads.
    class ToolRegistry
    {
    public:
        // Replaces an existing tool with the same name.
        void add(ToolDef tool);
        const ToolDef* find(std::string_view name) const;
        std::size_t size() const { return tools.size(); }

        // The `tools` array for a tools/list result, in registration order.
        nlohmann::json listJson() const;

    private:
        std::vector<ToolDef> tools;
        std::unordered_map<std::string, std::size_t> indexByName;
    };
}
