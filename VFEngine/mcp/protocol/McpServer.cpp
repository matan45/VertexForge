#include "McpServer.hpp"
#include "ArgReader.hpp"
#include "../dispatch/MainThreadQueue.hpp"

#include <array>

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
    }

    McpServer::McpServer(const ToolRegistry& toolRegistry, Info serverInfo)
        : tools(toolRegistry), info(std::move(serverInfo))
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
                // No pagination: the tool set is small and static.
                return jsonrpc::makeResult(message.id, nlohmann::json{{"tools", tools.listJson()}});
            }
            if (message.method == "tools/call")
            {
                return jsonrpc::makeResult(message.id, handleToolsCall(params));
            }
            return jsonrpc::makeError(message.id, jsonrpc::errc::methodNotFound,
                                      "Method not found: " + message.method);
        }
        catch (const jsonrpc::RpcError& e)
        {
            return jsonrpc::makeError(message.id, e.code(), e.what());
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
                {"tools", nlohmann::json{{"listChanged", false}}}
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

        const ToolDef* tool = tools.find(name);
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
