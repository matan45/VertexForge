#pragma once

#include "../protocol/ToolRegistry.hpp"
#include "events/EventDispatcher.hpp"

#include <atomic>
#include <functional>
#include <string>

namespace mcp::tools
{
    // VK-1652: mirrors the plugin-contributed MCP tools (ListPluginMcpToolsQuery)
    // into the ToolRegistry under owner "plugins". Every generated ToolDef handler
    // captures only the qualified tool name and executes InvokePluginMcpToolCommand,
    // so the registry never holds plugin code.
    //
    // Threading: subscribe/unsubscribe/syncIfDirty on the main thread only.
    class PluginToolBridge
    {
    public:
        static constexpr const char* owner = "plugins";

        // Called with a complete JSON-RPC notification text after a sync that
        // changed the listing (McpService broadcasts it on the SSE streams).
        using ListChangedCallback = std::function<void(const std::string& notificationJson)>;

        PluginToolBridge(ToolRegistry& registry, ListChangedCallback onListChanged);
        ~PluginToolBridge();

        PluginToolBridge(const PluginToolBridge&) = delete;
        PluginToolBridge& operator=(const PluginToolBridge&) = delete;

        // Subscribes to PluginMcpToolsChangedNotification (marks dirty).
        void subscribe();
        void unsubscribe();

        void markDirty() { dirty.store(true, std::memory_order_release); }
        bool isDirty() const { return dirty.load(std::memory_order_acquire); }

        // Re-queries the plugin tools and swaps the "plugins" group when dirty.
        // Never throws (a missing handler or failing query logs and keeps the old
        // group). Calls the callback once when the tools/list output changed and
        // `notify` is true. Returns true when the listing changed.
        bool syncIfDirty(bool notify = true);

    private:
        ToolRegistry& registry;
        ListChangedCallback listChanged;
        std::atomic<bool> dirty{true};  // starts dirty: plugins load before the server
        events::ScopedSubscription subscription;
    };
}
