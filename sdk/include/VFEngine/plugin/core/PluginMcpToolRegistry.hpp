#pragma once
#include "../api/PluginMcpTool.hpp"
#include "../../services/events/editor/PluginMcpToolEvents.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace plugin
{
    // VK-1652: process-wide registry of the MCP tools plugins contribute through
    // PluginContext::registerMcpTool. The handlers (std::functions instantiated in the
    // plugin DLLs) never leave this registry: the MCP server lists and invokes tools by
    // qualified name through events::editor::ListPluginMcpToolsQuery /
    // InvokePluginMcpToolCommand (editor/handlers/PluginMcpToolHandler), and
    // PluginContextImpl::cleanupAll drops a plugin's entries before its DLL unloads.
    //
    // Header-only (function-local static state) so the CPU-only Tests binary, which
    // does not link Plugin, can exercise it. Thread-safe; handlers run without the
    // lock held, so a handler may register / unregister tools (even itself).
    class PluginMcpToolRegistry
    {
    public:
        static constexpr std::size_t maxToolNameLength = 32;
        static constexpr std::size_t maxQualifiedNameLength = 48;  // Claude Code prefixes "mcp__vertexforge__"
        static constexpr uint32_t minTimeoutMs = 1000;
        static constexpr uint32_t maxTimeoutMs = 120000;

        struct RegisterOutcome
        {
            bool ok = false;
            bool replaced = false;      // same plugin re-registered the same tool name
            std::string qualifiedName;
            std::string error;          // set when !ok
        };

        // "<plugin>_<tool>" lower-cased; every character outside [a-z0-9] becomes '_',
        // runs of '_' collapse to one and leading/trailing '_' are trimmed.
        static std::string qualify(std::string_view pluginName, std::string_view toolName)
        {
            std::string raw;
            raw.reserve(pluginName.size() + 1 + toolName.size());
            raw.append(pluginName);
            raw.push_back('_');
            raw.append(toolName);
            return sanitize(raw);
        }

        static RegisterOutcome add(const std::string& pluginName, PluginMcpToolDesc desc)
        {
            RegisterOutcome outcome;
            outcome.qualifiedName = qualify(pluginName, desc.name);

            if (std::string error = validate(pluginName, desc, outcome.qualifiedName); !error.empty())
            {
                outcome.error = std::move(error);
                return outcome;
            }

            Entry entry;
            entry.qualifiedName = outcome.qualifiedName;
            entry.pluginName = pluginName;
            entry.toolName = desc.name;
            entry.title = std::move(desc.title);
            entry.description = std::move(desc.description);
            entry.inputSchema = std::move(desc.inputSchema);
            entry.readOnly = desc.readOnly;
            entry.destructive = desc.destructive;
            entry.timeoutMs = std::clamp(desc.timeoutMs, minTimeoutMs, maxTimeoutMs);
            entry.handler = std::make_shared<PluginMcpToolHandler>(std::move(desc.handler));

            std::shared_ptr<PluginMcpToolHandler> previousHandler;  // destroyed after unlock
            {
                auto& s = state();
                std::lock_guard lock(s.mutex);
                entry.active = !s.inactivePlugins.contains(pluginName);

                auto it = std::find_if(s.entries.begin(), s.entries.end(), [&](const Entry& e) {
                    return e.qualifiedName == entry.qualifiedName;
                });
                if (it == s.entries.end())
                {
                    s.entries.push_back(std::move(entry));
                }
                else if (it->pluginName == pluginName && it->toolName == entry.toolName)
                {
                    previousHandler = std::move(it->handler);
                    *it = std::move(entry);
                    outcome.replaced = true;
                }
                else
                {
                    outcome.error = "qualified name '" + outcome.qualifiedName + "' is already used by plugin '"
                        + it->pluginName + "' (tool '" + it->toolName + "')";
                    return outcome;
                }
            }

            outcome.ok = true;
            return outcome;
        }

        static bool remove(const std::string& pluginName, const std::string& toolName)
        {
            std::vector<Entry> removed;  // handlers destroyed after unlock
            {
                auto& s = state();
                std::lock_guard lock(s.mutex);
                extractIf(s.entries, removed, [&](const Entry& e) {
                    return e.pluginName == pluginName && e.toolName == toolName;
                });
            }
            return !removed.empty();
        }

        // Drops every tool of the plugin (unload). Returns whether any was removed.
        static bool removeByPlugin(const std::string& pluginName)
        {
            std::vector<Entry> removed;
            {
                auto& s = state();
                std::lock_guard lock(s.mutex);
                extractIf(s.entries, removed, [&](const Entry& e) { return e.pluginName == pluginName; });
                s.inactivePlugins.erase(pluginName);
            }
            return !removed.empty();
        }

        // VK-1365 per-scene soft-disable: inactive plugins' tools are hidden from
        // listActive() and refuse invoke(). Also remembered for tools registered later.
        // Returns whether the plugin has any tools (i.e. whether the listing may change).
        static bool setPluginActive(const std::string& pluginName, bool active)
        {
            auto& s = state();
            std::lock_guard lock(s.mutex);
            if (active)
            {
                s.inactivePlugins.erase(pluginName);
            }
            else
            {
                s.inactivePlugins.insert(pluginName);
            }

            bool hasAny = false;
            for (auto& entry : s.entries)
            {
                if (entry.pluginName == pluginName)
                {
                    entry.active = active;
                    hasAny = true;
                }
            }
            return hasAny;
        }

        static bool hasTools(const std::string& pluginName)
        {
            auto& s = state();
            std::lock_guard lock(s.mutex);
            return std::any_of(s.entries.begin(), s.entries.end(),
                               [&](const Entry& e) { return e.pluginName == pluginName; });
        }

        // Tools of active plugins, in registration order.
        static std::vector<events::editor::PluginMcpToolInfo> listActive()
        {
            auto& s = state();
            std::lock_guard lock(s.mutex);
            std::vector<events::editor::PluginMcpToolInfo> list;
            list.reserve(s.entries.size());
            for (const auto& entry : s.entries)
            {
                if (!entry.active)
                {
                    continue;
                }
                events::editor::PluginMcpToolInfo info;
                info.qualifiedName = entry.qualifiedName;
                info.plugin = entry.pluginName;
                info.title = entry.title;
                info.description = entry.description;
                info.inputSchema = entry.inputSchema;
                info.readOnly = entry.readOnly;
                info.destructive = entry.destructive;
                info.timeoutMs = entry.timeoutMs;
                list.push_back(std::move(info));
            }
            return list;
        }

        // Runs the tool's handler on the calling thread (the editor main thread in
        // practice). The handler is copied out under the lock, so it survives the tool
        // being unregistered mid-call. Exceptions become isError results.
        static events::editor::PluginMcpToolInvokeResult invoke(const std::string& qualifiedName,
                                                                const nlohmann::json& arguments)
        {
            std::shared_ptr<PluginMcpToolHandler> handler;
            {
                auto& s = state();
                std::lock_guard lock(s.mutex);
                for (const auto& entry : s.entries)
                {
                    if (entry.qualifiedName == qualifiedName)
                    {
                        if (entry.active)
                        {
                            handler = entry.handler;
                        }
                        break;
                    }
                }
            }

            events::editor::PluginMcpToolInvokeResult result;
            if (!handler || !*handler)
            {
                result.text = "tool '" + qualifiedName + "' is no longer available";
                result.isError = true;
                return result;
            }

            try
            {
                PluginMcpToolResult toolResult = (*handler)(arguments);
                result.structured = std::move(toolResult.structured);
                result.text = std::move(toolResult.text);
                result.isError = toolResult.isError;
            }
            catch (const std::exception& e)
            {
                result = {};
                result.text = "tool '" + qualifiedName + "' threw: " + e.what();
                result.isError = true;
            }
            catch (...)
            {
                result = {};
                result.text = "tool '" + qualifiedName + "' threw an unknown exception";
                result.isError = true;
            }
            return result;
        }

        static void clearForTests()
        {
            std::vector<Entry> removed;
            {
                auto& s = state();
                std::lock_guard lock(s.mutex);
                removed.swap(s.entries);
                s.inactivePlugins.clear();
            }
        }

    private:
        struct Entry
        {
            std::string qualifiedName;
            std::string pluginName;
            std::string toolName;
            std::string title;
            std::string description;
            nlohmann::json inputSchema;
            bool readOnly = false;
            bool destructive = false;
            uint32_t timeoutMs = 10000;
            std::shared_ptr<PluginMcpToolHandler> handler;
            bool active = true;
        };

        struct State
        {
            std::mutex mutex;
            std::vector<Entry> entries;
            std::unordered_set<std::string> inactivePlugins;
        };

        static State& state()
        {
            static State instance;
            return instance;
        }

        static std::string sanitize(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (char c : text)
            {
                const auto u = static_cast<unsigned char>(c);
                char mapped = '_';
                if (u >= 'A' && u <= 'Z')
                {
                    mapped = static_cast<char>(u - 'A' + 'a');
                }
                else if ((u >= 'a' && u <= 'z') || (u >= '0' && u <= '9'))
                {
                    mapped = c;
                }

                if (mapped == '_' && (out.empty() || out.back() == '_'))
                {
                    continue;  // collapse runs, drop leading '_'
                }
                out.push_back(mapped);
            }
            if (!out.empty() && out.back() == '_')
            {
                out.pop_back();
            }
            return out;
        }

        static bool isValidToolName(std::string_view name)
        {
            if (name.empty() || name.size() > maxToolNameLength)
            {
                return false;
            }
            bool hasAlnum = false;
            for (char c : name)
            {
                const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
                if (!alnum && c != '_')
                {
                    return false;
                }
                hasAlnum = hasAlnum || alnum;
            }
            return hasAlnum;  // "_" / "__" would vanish from the qualified name
        }

        // Empty when valid, otherwise the reason.
        static std::string validate(const std::string& pluginName, const PluginMcpToolDesc& desc,
                                    const std::string& qualifiedName)
        {
            if (sanitize(pluginName).empty())
            {
                return "plugin name '" + pluginName + "' has no [A-Za-z0-9] characters";
            }
            if (!isValidToolName(desc.name))
            {
                return "tool name '" + desc.name + "' must match [a-z0-9_]{1,32} with at least one letter or digit";
            }
            if (!desc.handler)
            {
                return "tool '" + desc.name + "' has no handler";
            }
            const auto type = desc.inputSchema.is_object() ? desc.inputSchema.find("type") : desc.inputSchema.end();
            if (!desc.inputSchema.is_object() || type == desc.inputSchema.end() || *type != "object")
            {
                return "tool '" + desc.name + "' inputSchema must be a JSON object with \"type\": \"object\"";
            }
            if (qualifiedName.size() > maxQualifiedNameLength)
            {
                return "qualified name '" + qualifiedName + "' is longer than "
                    + std::to_string(maxQualifiedNameLength) + " characters";
            }
            return {};
        }

        template <typename Pred>
        static void extractIf(std::vector<Entry>& entries, std::vector<Entry>& out, Pred pred)
        {
            auto split = std::stable_partition(entries.begin(), entries.end(),
                                               [&](const Entry& e) { return !pred(e); });
            out.insert(out.end(), std::make_move_iterator(split), std::make_move_iterator(entries.end()));
            entries.erase(split, entries.end());
        }
    };
}
