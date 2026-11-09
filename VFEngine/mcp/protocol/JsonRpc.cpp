#include "JsonRpc.hpp"

namespace mcp::jsonrpc
{
    namespace
    {
        bool isValidId(const nlohmann::json& id)
        {
            // JSON-RPC allows string, number or null; MCP forbids null ids on requests.
            return id.is_string() || id.is_number_integer() || id.is_number_unsigned();
        }
    }

    ParseOutcome parse(std::string_view body)
    {
        ParseOutcome outcome;

        nlohmann::json doc = nlohmann::json::parse(body, nullptr, false);
        if (doc.is_discarded())
        {
            outcome.errorResponse = makeError(nullptr, errc::parseError, "Parse error");
            return outcome;
        }

        if (!doc.is_object())
        {
            outcome.errorResponse = makeError(nullptr, errc::invalidRequest,
                                              doc.is_array() ? "Batch requests are not supported"
                                                             : "Request must be a JSON object");
            return outcome;
        }

        auto versionIt = doc.find("jsonrpc");
        if (versionIt == doc.end() || !versionIt->is_string() || versionIt->get<std::string>() != "2.0")
        {
            nlohmann::json id = doc.contains("id") && isValidId(doc["id"]) ? doc["id"] : nlohmann::json(nullptr);
            outcome.errorResponse = makeError(id, errc::invalidRequest, "jsonrpc must be \"2.0\"");
            return outcome;
        }

        Message message;
        auto methodIt = doc.find("method");
        auto idIt = doc.find("id");

        if (methodIt == doc.end())
        {
            // A response (result/error) sent by the client. We never issue
            // server->client requests in P1, so it is acknowledged and dropped.
            if (doc.contains("result") || doc.contains("error"))
            {
                message.kind = MessageKind::Response;
                message.id = idIt != doc.end() ? *idIt : nlohmann::json(nullptr);
                outcome.message = std::move(message);
                return outcome;
            }
            nlohmann::json id = idIt != doc.end() && isValidId(*idIt) ? *idIt : nlohmann::json(nullptr);
            outcome.errorResponse = makeError(id, errc::invalidRequest, "Missing method");
            return outcome;
        }

        if (!methodIt->is_string())
        {
            nlohmann::json id = idIt != doc.end() && isValidId(*idIt) ? *idIt : nlohmann::json(nullptr);
            outcome.errorResponse = makeError(id, errc::invalidRequest, "method must be a string");
            return outcome;
        }
        message.method = methodIt->get<std::string>();

        if (auto paramsIt = doc.find("params"); paramsIt != doc.end())
        {
            if (!paramsIt->is_object() && !paramsIt->is_array())
            {
                nlohmann::json id = idIt != doc.end() && isValidId(*idIt) ? *idIt : nlohmann::json(nullptr);
                outcome.errorResponse = makeError(id, errc::invalidRequest, "params must be an object or array");
                return outcome;
            }
            message.params = *paramsIt;
        }

        if (idIt == doc.end())
        {
            message.kind = MessageKind::Notification;
        }
        else
        {
            if (!isValidId(*idIt))
            {
                outcome.errorResponse = makeError(nullptr, errc::invalidRequest, "id must be a string or integer");
                return outcome;
            }
            message.kind = MessageKind::Request;
            message.id = *idIt;
        }

        outcome.message = std::move(message);
        return outcome;
    }

    nlohmann::json makeResult(const nlohmann::json& id, nlohmann::json result)
    {
        return nlohmann::json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", std::move(result)}
        };
    }

    nlohmann::json makeError(const nlohmann::json& id, int code, std::string_view message,
                             const nlohmann::json& data)
    {
        nlohmann::json error{
            {"code", code},
            {"message", std::string(message)}
        };
        if (!data.is_null())
        {
            error["data"] = data;
        }
        return nlohmann::json{
            {"jsonrpc", "2.0"},
            {"id", id},
            {"error", std::move(error)}
        };
    }
}
