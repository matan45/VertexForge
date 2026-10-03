#pragma once
#include "../EventTypes.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// VK-1652: MCP tools contributed by plugins (PluginContext::registerMcpTool).
// The plugin handlers stay in the Plugin runtime (plugin/core/PluginMcpToolRegistry);
// the MCP server (Mcp StaticLib) reaches them only through these events, by name,
// so nothing it holds can outlive a plugin DLL. Handled in Editor.exe by
// editor/handlers/PluginMcpToolHandler.
namespace events::editor {

    struct PluginMcpToolInfo {
        std::string qualifiedName;  // "<plugin>_<tool>", unique, what the agent calls
        std::string plugin;
        std::string title;
        std::string description;
        nlohmann::json inputSchema;
        bool readOnly = false;
        bool destructive = false;
        uint32_t timeoutMs = 10000;
    };

    struct PluginMcpToolInvokeResult {
        nlohmann::json structured;
        std::string text;
        bool isError = false;
    };

    // Tools of currently active plugins only (VK-1365 per-scene state).
    struct ListPluginMcpToolsQuery : IQuery<std::vector<PluginMcpToolInfo>> {
        std::string_view getName() const override { return "ListPluginMcpTools"; }
    };

    // Main thread. Unknown, unregistered or inactive tool -> isError.
    struct InvokePluginMcpToolCommand : ICommand<PluginMcpToolInvokeResult> {
        std::string qualifiedName;
        nlohmann::json arguments;

        std::string_view getName() const override { return "InvokePluginMcpTool"; }
    };

    // A plugin registered / unregistered a tool, or a plugin with tools changed
    // its active state. Published on the main thread.
    struct PluginMcpToolsChangedNotification : INotification {
        std::string_view getName() const override { return "PluginMcpToolsChanged"; }
    };
}
