#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <nlohmann/json.hpp>

// VK-1652 (API v22): a tool a plugin contributes to the Editor's MCP server, so an
// AI agent (Claude Code, ...) can call it next to the built-in tools.
//
// Registered through PluginContext::registerMcpTool. The handler always runs on the
// editor main thread, so it may touch the registry and dispatch engine events. The
// engine keeps the handler inside the plugin runtime and drops it in cleanup before
// the DLL unloads; the MCP server only ever sees the tool's name.
namespace plugin {

    struct PluginMcpToolResult {
        nlohmann::json structured;  // returned as structuredContent (wrapped in {"result":..} when not an object)
        std::string text;           // human-readable text; defaults to structured.dump(2)
        bool isError = false;

        static PluginMcpToolResult ok(nlohmann::json value, std::string message = {}) {
            PluginMcpToolResult result;
            result.structured = std::move(value);
            result.text = std::move(message);
            return result;
        }

        static PluginMcpToolResult error(std::string message) {
            PluginMcpToolResult result;
            result.text = std::move(message);
            result.isError = true;
            return result;
        }
    };

    using PluginMcpToolHandler = std::function<PluginMcpToolResult(const nlohmann::json& arguments)>;

    struct PluginMcpToolDesc {
        // [a-z0-9_], 1-32 chars. The agent sees "<plugin>_<name>" (plugin name
        // lower-cased, other characters replaced by '_'), at most 48 chars.
        std::string name;
        std::string title;
        std::string description;
        // JSON Schema of the arguments; "type" must be "object".
        nlohmann::json inputSchema = nlohmann::json{{"type", "object"}, {"properties", nlohmann::json::object()}};
        bool readOnly = false;     // annotations.readOnlyHint
        bool destructive = false;  // annotations.destructiveHint
        uint32_t timeoutMs = 10000;  // clamped to [1000, 120000]
        PluginMcpToolHandler handler;
    };
}
