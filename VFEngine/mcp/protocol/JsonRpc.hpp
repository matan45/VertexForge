#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

// JSON-RPC 2.0 framing for the MCP server (VK-1650).
// MCP 2025-06-18 removed batch support, so a top-level array is rejected as an
// invalid request rather than fanned out.
namespace mcp::jsonrpc
{
    namespace errc
    {
        constexpr int parseError = -32700;
        constexpr int invalidRequest = -32600;
        constexpr int methodNotFound = -32601;
        constexpr int invalidParams = -32602;
        constexpr int internalError = -32603;
        // Implementation-defined (-32000..-32099): the editor main thread did not
        // pick the request up within the tool's timeout.
        constexpr int mainThreadTimeout = -32001;
    }

    enum class MessageKind
    {
        Request,       // has id + method -> expects a response
        Notification,  // method, no id -> no response
        Response       // result/error from the client (we never send requests, so ignored)
    };

    struct Message
    {
        MessageKind kind = MessageKind::Request;
        nlohmann::json id;      // string or integer; null for notifications/responses without id
        std::string method;
        nlohmann::json params;  // object, array or null
    };

    // Either a parsed message, or a ready-to-send error response (parse error /
    // invalid request). Exactly one of the two is engaged.
    struct ParseOutcome
    {
        std::optional<Message> message;
        std::optional<nlohmann::json> errorResponse;
    };

    ParseOutcome parse(std::string_view body);

    nlohmann::json makeResult(const nlohmann::json& id, nlohmann::json result);
    nlohmann::json makeError(const nlohmann::json& id, int code, std::string_view message,
                             const nlohmann::json& data = nullptr);

    // Thrown by method handlers to produce a JSON-RPC error response.
    class RpcError : public std::runtime_error
    {
    public:
        RpcError(int code, const std::string& message)
            : std::runtime_error(message), errorCode(code)
        {
        }

        int code() const noexcept { return errorCode; }

    private:
        int errorCode;
    };
}
