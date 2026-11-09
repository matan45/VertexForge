#include "McpServer.hpp"
#include "ArgReader.hpp"
#include "../dispatch/MainThreadQueue.hpp"

#include <array>
#include <unordered_set>

namespace mcp
{
    namespace
    {
        // Versions we can speak. The 2024/2025-03 clients simply ignore
        // structuredContent / annotations they do not know.
        constexpr std::array<const char*, 3> supportedVersions{
            "2025-06-18", "2025-03-26", "2024-11-05"
        };

        bool isSupported(const std::string& version)
        {
            for (const char* v : supportedVersions)
            {
                if (version == v)
                {
                    return true;
                }
            }
            return false;
        }

        // Contributors carry no timeout of their own.
        constexpr std::chrono::milliseconds contributorTimeout{10000};

        nlohmann::json contentsJson(ResourceContents contents, const std::string& requestedUri,
                                    const std::string& defaultMimeType)
        {
            return nlohmann::json{
                {"uri", contents.uri.empty() ? requestedUri : std::move(contents.uri)},
                {"mimeType", contents.mimeType.empty() ? defaultMimeType : std::move(contents.mimeType)},
                {"text", std::move(contents.text)}
            };
        }
    }

    McpServer::McpServer(const ToolRegistry& toolRegistry, const ResourceRegistry& resourceRegistry,
                         const PromptRegistry& promptRegistry, Info serverInfo)
        : tools(toolRegistry), resources(resourceRegistry), prompts(promptRegistry), info(std::move(serverInfo))
    {
    }

    void McpServer::setMainThreadInvoker(MainThreadInvoker mainThreadInvoker)
    {
        invoker = std::move(mainThreadInvoker);
    }

    std::optional<nlohmann::json> McpServer::handleBody(std::string_view body)
    {
        jsonrpc::ParseOutcome outcome = jsonrpc::parse(body);
        if (outcome.errorResponse)
        {
            return std::move(*outcome.errorResponse);
        }
        return handle(*outcome.message);
    }

    std::optional<nlohmann::json> McpServer::handle(const jsonrpc::Message& message)
    {
        if (message.kind != jsonrpc::MessageKind::Request)
        {
            // notifications/initialized, notifications/cancelled, client responses:
            // nothing to do and nothing to send.
            return std::nullopt;
        }

        const nlohmann::json params = message.params.is_null() ? nlohmann::json::object() : message.params;

        try
        {
            if (message.method == "initialize")
            {
                return jsonrpc::makeResult(message.id, handleInitialize(params));
            }
            if (message.method == "ping")
            {
                return jsonrpc::makeResult(message.id, nlohmann::json::object());
            }
            if (message.method == "tools/list")
            {
                // No pagination: the tool set is small. Plugin tools change it at
                // runtime (notifications/tools/list_changed).
                return jsonrpc::makeResult(message.id, nlohmann::json{{"tools", tools.listJson()}});
            }
            if (message.method == "tools/call")
            {
                return jsonrpc::makeResult(message.id, handleToolsCall(params));
            }
            if (message.method == "resources/list")
            {
                // No pagination: any cursor is ignored.
                return jsonrpc::makeResult(message.id, handleResourcesList());
            }
            if (message.method == "resources/templates/list")
            {
                nlohmann::json list = nlohmann::json::array();
                for (const ResourceTemplateDef& resourceTemplate : resources.templates())
                {
                    list.push_back(ResourceRegistry::describe(resourceTemplate));
                }
                return jsonrpc::makeResult(message.id, nlohmann::json{{"resourceTemplates", std::move(list)}});
            }
            if (message.method == "resources/read")
            {
                return jsonrpc::makeResult(message.id, handleResourcesRead(params));
            }
            if (message.method == "prompts/list")
            {
                return jsonrpc::makeResult(message.id, nlohmann::json{{"prompts", prompts.listJson()}});
            }
            if (message.method == "prompts/get")
            {
                return jsonrpc::makeResult(message.id, handlePromptsGet(params));
            }
            return jsonrpc::makeError(message.id, jsonrpc::errc::methodNotFound,
                                      "Method not found: " + message.method);
        }
        catch (const jsonrpc::RpcError& e)
        {
            return jsonrpc::makeError(message.id, e.code(), e.what(), e.data());
        }
        catch (const std::exception& e)
        {
            return jsonrpc::makeError(message.id, jsonrpc::errc::internalError, e.what());
        }
    }

    nlohmann::json McpServer::handleInitialize(const nlohmann::json& params)
    {
        std::string requested;
        if (auto it = params.find("protocolVersion"); it != params.end() && it->is_string())
        {
            requested = it->get<std::string>();
        }

        if (auto clientInfo = params.find("clientInfo"); clientInfo != params.end() && clientInfo->is_object())
        {
            std::lock_guard<std::mutex> lock(statsMutex);
            connectedClient = clientInfo->value("name", std::string("unknown"));
            if (clientInfo->contains("version") && (*clientInfo)["version"].is_string())
            {
                connectedClient += " " + (*clientInfo)["version"].get<std::string>();
            }
        }

        nlohmann::json result{
            {"protocolVersion", isSupported(requested) ? requested : std::string(latestProtocolVersion)},
            {"capabilities", nlohmann::json{
                {"tools", nlohmann::json{{"listChanged", true}}},
                {"resources", nlohmann::json{{"subscribe", false}, {"listChanged", false}}},
                {"prompts", nlohmann::json{{"listChanged", false}}}
            }},
            {"serverInfo", nlohmann::json{
                {"name", info.name},
                {"title", info.title},
                {"version", info.version}
            }}
        };
        if (!info.instructions.empty())
        {
            result["instructions"] = info.instructions;
        }
        return result;
    }

    nlohmann::json McpServer::handleToolsCall(const nlohmann::json& params)
    {
        auto nameIt = params.find("name");
        if (nameIt == params.end() || !nameIt->is_string())
        {
            throw jsonrpc::RpcError(jsonrpc::errc::invalidParams, "tools/call requires a string 'name'");
        }
        const std::string name = nameIt->get<std::string>();

        // Shared ownership: a plugin tool swapped out mid-call stays alive until
        // this call (and its queued main-thread task) is done with it.
        std::shared_ptr<const ToolDef> tool = tools.find(name);
        if (!tool)
        {
            throw jsonrpc::RpcError(jsonrpc::errc::invalidParams, "Unknown tool: " + name);
        }

        nlohmann::json arguments = nlohmann::json::object();
        if (auto argsIt = params.find("arguments"); argsIt != params.end() && !argsIt->is_null())
        {
            if (!argsIt->is_object())
            {
                throw jsonrpc::RpcError(jsonrpc::errc::invalidParams, "tools/call 'arguments' must be an object");
            }
            arguments = *argsIt;
        }

        {
            std::lock_guard<std::mutex> lock(statsMutex);
            lastTool = name;
        }
        toolCalls.fetch_add(1, std::memory_order_relaxed);

        // Tool failures are reported in-band (isError) so the model sees them and
        // can recover; only a main-thread timeout is a protocol-level error.
        // Captures by value: after a timeout the queued task may still run on the
        // main thread once this frame has returned.
        auto runTool = [tool, arguments = std::move(arguments)]() -> nlohmann::json
        {
            try
            {
                return tool->handler(arguments).toJson();
            }
            catch (const ArgError& e)
            {
                return ToolResult::error(std::string("Invalid arguments: ") + e.what()).toJson();
            }
            catch (const std::exception& e)
            {
                return ToolResult::error(e.what()).toJson();
            }
            catch (...)
            {
                return ToolResult::error("tool failed with an unknown exception").toJson();
            }
        };

        nlohmann::json result;
        try
        {
            if (tool->affinity == ThreadAffinity::Main && invoker)
            {
                result = invoker(runTool, tool->timeout);
            }
            else
            {
                result = runTool();
            }
        }
        catch (const MainThreadTimeout& e)
        {
            toolErrors.fetch_add(1, std::memory_order_relaxed);
            throw jsonrpc::RpcError(jsonrpc::errc::mainThreadTimeout, e.what());
        }
        catch (const QueueShutdown& e)
        {
            result = ToolResult::error(e.what()).toJson();
        }

        if (result.value("isError", false))
        {
            toolErrors.fetch_add(1, std::memory_order_relaxed);
        }
        return result;
    }

    nlohmann::json McpServer::handleResourcesList()
    {
        nlohmann::json list = nlohmann::json::array();
        std::unordered_set<std::string> seen;
        for (const ResourceDef& resource : resources.resources())
        {
            if (seen.insert(resource.uri).second)
            {
                list.push_back(ResourceRegistry::describe(resource));
            }
        }

        for (const ResourceListContributor& contributor : resources.contributors())
        {
            if (!contributor.list)
            {
                continue;
            }

            // Captures a copy: after a timeout the queued task may still run.
            auto runList = [listFn = contributor.list]() -> nlohmann::json
            {
                nlohmann::json out = nlohmann::json::array();
                for (const ResourceDef& resource : listFn())
                {
                    out.push_back(ResourceRegistry::describe(resource));
                }
                return out;
            };

            nlohmann::json contributed;
            try
            {
                contributed = contributor.affinity == ThreadAffinity::Main && invoker
                    ? invoker(runList, contributorTimeout)
                    : runList();
            }
            catch (const std::exception&)
            {
                // A failing contributor (timeout, shutdown, no handler) only hides
                // its own entries.
                continue;
            }
            catch (...)
            {
                continue;
            }

            for (nlohmann::json& entry : contributed)
            {
                if (seen.insert(entry.value("uri", std::string())).second)
                {
                    list.push_back(std::move(entry));
                }
            }
        }
        return nlohmann::json{{"resources", std::move(list)}};
    }

    nlohmann::json McpServer::handleResourcesRead(const nlohmann::json& params)
    {
        auto uriIt = params.find("uri");
        if (uriIt == params.end() || !uriIt->is_string())
        {
            throw jsonrpc::RpcError(jsonrpc::errc::invalidParams, "resources/read requires a string 'uri'");
        }
        const std::string uri = uriIt->get<std::string>();

        // The task copies the reader: after a timeout it may still run on the main
        // thread once this frame has returned.
        std::function<nlohmann::json()> runReader;
        ThreadAffinity affinity = ThreadAffinity::Main;
        std::chrono::milliseconds timeout{10000};

        if (const ResourceDef* resource = resources.findExact(uri))
        {
            affinity = resource->affinity;
            timeout = resource->timeout;
            runReader = [reader = resource->reader, uri, mimeType = resource->mimeType]() -> nlohmann::json
            {
                return contentsJson(reader(), uri, mimeType);
            };
        }
        else if (const ResourceTemplateDef* resourceTemplate = resources.findTemplate(uri))
        {
            std::optional<std::string> suffix =
                ResourceRegistry::percentDecode(std::string_view(uri).substr(resourceTemplate->prefix.size()));
            if (!suffix)
            {
                throw jsonrpc::RpcError(jsonrpc::errc::invalidParams,
                                        "Invalid resource URI (bad percent-encoding): " + uri);
            }
            affinity = resourceTemplate->affinity;
            timeout = resourceTemplate->timeout;
            runReader = [reader = resourceTemplate->reader, uri, suffix = std::move(*suffix),
                         mimeType = resourceTemplate->mimeType]() -> nlohmann::json
            {
                return contentsJson(reader(uri, suffix), uri, mimeType);
            };
        }

        if (!runReader)
        {
            throw jsonrpc::RpcError(jsonrpc::errc::resourceNotFound, "Resource not found: " + uri,
                                    nlohmann::json{{"uri", uri}});
        }

        nlohmann::json contents;
        try
        {
            contents = affinity == ThreadAffinity::Main && invoker ? invoker(runReader, timeout) : runReader();
        }
        catch (const ResourceNotFound& e)
        {
            throw jsonrpc::RpcError(jsonrpc::errc::resourceNotFound,
                                    "Resource not found: " + uri + " (" + e.what() + ")",
                                    nlohmann::json{{"uri", uri}});
        }
        catch (const ArgError& e)
        {
            // A malformed URI suffix (e.g. an absolute script path, a bad module id).
            throw jsonrpc::RpcError(jsonrpc::errc::invalidParams, std::string("Invalid resource URI: ") + e.what(),
                                    nlohmann::json{{"uri", uri}});
        }
        catch (const MainThreadTimeout& e)
        {
            throw jsonrpc::RpcError(jsonrpc::errc::mainThreadTimeout, e.what());
        }
        catch (const std::exception& e)
        {
            throw jsonrpc::RpcError(jsonrpc::errc::internalError, e.what());
        }
        catch (...)
        {
            throw jsonrpc::RpcError(jsonrpc::errc::internalError, "resource reader failed with an unknown exception");
        }
        return nlohmann::json{{"contents", nlohmann::json::array({std::move(contents)})}};
    }

    nlohmann::json McpServer::handlePromptsGet(const nlohmann::json& params)
    {
        auto nameIt = params.find("name");
        if (nameIt == params.end() || !nameIt->is_string())
        {
            throw jsonrpc::RpcError(jsonrpc::errc::invalidParams, "prompts/get requires a string 'name'");
        }
        const std::string name = nameIt->get<std::string>();

        const PromptDef* prompt = prompts.find(name);
        if (!prompt)
        {
            throw jsonrpc::RpcError(jsonrpc::errc::invalidParams, "Unknown prompt: " + name);
        }

        // MCP prompt arguments are a string -> string map.
        nlohmann::json arguments = nlohmann::json::object();
        if (auto argsIt = params.find("arguments"); argsIt != params.end() && !argsIt->is_null())
        {
            if (!argsIt->is_object())
            {
                throw jsonrpc::RpcError(jsonrpc::errc::invalidParams, "prompts/get 'arguments' must be an object");
            }
            for (auto it = argsIt->begin(); it != argsIt->end(); ++it)
            {
                if (!it.value().is_string())
                {
                    throw jsonrpc::RpcError(jsonrpc::errc::invalidParams,
                                            "prompt argument '" + it.key() + "' must be a string");
                }
            }
            arguments = *argsIt;
        }

        for (const PromptArgument& argument : prompt->arguments)
        {
            if (argument.required && !arguments.contains(argument.name))
            {
                throw jsonrpc::RpcError(jsonrpc::errc::invalidParams,
                                        "missing required prompt argument '" + argument.name + "'");
            }
        }

        if (!prompt->build)
        {
            throw jsonrpc::RpcError(jsonrpc::errc::internalError, "prompt has no builder: " + name);
        }

        nlohmann::json messages;
        try
        {
            messages = prompt->build(arguments);
        }
        catch (const ArgError& e)
        {
            throw jsonrpc::RpcError(jsonrpc::errc::invalidParams, std::string("Invalid arguments: ") + e.what());
        }
        return nlohmann::json{
            {"description", prompt->description},
            {"messages", std::move(messages)}
        };
    }

    std::string McpServer::clientName() const
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        return connectedClient;
    }

    std::string McpServer::lastToolName() const
    {
        std::lock_guard<std::mutex> lock(statsMutex);
        return lastTool;
    }
}
