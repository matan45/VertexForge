#include "PluginToolBridge.hpp"
#include "print/Log.hpp"

#include "events/editor/PluginMcpToolEvents.hpp"

#include <chrono>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace mcp::tools
{
    namespace
    {
        constexpr const char* listChangedNotification =
            R"({"jsonrpc":"2.0","method":"notifications/tools/list_changed"})";

        ToolDef toToolDef(const events::editor::PluginMcpToolInfo& info)
        {
            ToolDef def;
            def.name = info.qualifiedName;
            def.title = info.title;
            def.description = "[plugin " + info.plugin + "] " + info.description;
            def.inputSchema = info.inputSchema;
            def.readOnly = info.readOnly;
            def.destructive = info.destructive;
            def.timeout = std::chrono::milliseconds(info.timeoutMs);
            def.affinity = ThreadAffinity::Main;
            def.owner = PluginToolBridge::owner;
            // Captures only the name: the plugin handler stays in the Plugin runtime and
            // may be gone by the time this runs (the command then reports isError).
            def.handler = [qualifiedName = info.qualifiedName](const nlohmann::json& args) -> ToolResult {
                events::editor::InvokePluginMcpToolCommand cmd;
                cmd.qualifiedName = qualifiedName;
                cmd.arguments = args;
                events::editor::PluginMcpToolInvokeResult result = events::EventDispatcher::instance().execute(cmd);
                if (result.isError)
                {
                    return ToolResult::error(result.text.empty() ? std::string("plugin tool failed")
                                                                  : std::move(result.text));
                }
                return ToolResult::ok(std::move(result.structured), std::move(result.text));
            };
            return def;
        }
    }

    PluginToolBridge::PluginToolBridge(ToolRegistry& registry, ListChangedCallback onListChanged)
        : registry(registry)
        , listChanged(std::move(onListChanged))
    {
    }

    PluginToolBridge::~PluginToolBridge()
    {
        unsubscribe();
    }

    void PluginToolBridge::subscribe()
    {
        if (subscription.isValid())
        {
            return;
        }
        subscription = events::ScopedSubscription(
            events::EventDispatcher::instance().subscribe<events::editor::PluginMcpToolsChangedNotification>(
                [this](const events::editor::PluginMcpToolsChangedNotification&) { markDirty(); }));
    }

    void PluginToolBridge::unsubscribe()
    {
        subscription.unsubscribe();
    }

    bool PluginToolBridge::syncIfDirty(bool notify)
    {
        if (!dirty.exchange(false, std::memory_order_acq_rel))
        {
            return false;
        }

        std::vector<events::editor::PluginMcpToolInfo> infos;
        try
        {
            infos = events::EventDispatcher::instance().query(events::editor::ListPluginMcpToolsQuery{});
        }
        catch (const std::exception& e)
        {
            // No handler outside the editor (or plugins not up yet): keep the old group.
            vfLogDebug("[MCP] plugin tool listing unavailable: {}", e.what());
            return false;
        }
        catch (...)
        {
            vfLogDebug("[MCP] plugin tool listing unavailable");
            return false;
        }

        std::vector<ToolDef> defs;
        defs.reserve(infos.size());
        for (const auto& info : infos)
        {
            defs.push_back(toToolDef(info));
        }

        std::vector<std::string> rejected;
        const bool changed = registry.replaceGroup(owner, std::move(defs), &rejected);
        for (const auto& name : rejected)
        {
            vfLogWarning("[MCP] plugin tool '{}' ignored: the name is already used by a built-in tool", name);
        }

        if (changed && notify && listChanged)
        {
            listChanged(listChangedNotification);
        }
        return changed;
    }
}
