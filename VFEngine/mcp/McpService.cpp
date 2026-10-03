#include "McpService.hpp"
#include "print/Log.hpp"

#include <random>

namespace mcp
{
    namespace
    {
        std::string makeSessionId()
        {
            std::random_device rd;
            std::mt19937_64 gen((static_cast<uint64_t>(rd()) << 32) ^ rd());
            constexpr char hex[] = "0123456789abcdef";
            std::string id;
            id.reserve(32);
            for (int i = 0; i < 2; ++i)
            {
                uint64_t v = gen();
                for (int n = 0; n < 16; ++n)
                {
                    id.push_back(hex[v & 0xF]);
                    v >>= 4;
                }
            }
            return id;
        }

        bool isJsonContentType(std::string_view value)
        {
            // "application/json" optionally followed by "; charset=utf-8".
            constexpr std::string_view wanted = "application/json";
            if (value.size() < wanted.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < wanted.size(); ++i)
            {
                char c = value[i];
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
                if (c != wanted[i])
                {
                    return false;
                }
            }
            return value.size() == wanted.size() || value[wanted.size()] == ';' || value[wanted.size()] == ' ';
        }
    }

    McpService::McpService(McpServer::Info info)
        : server(tools, std::move(info)), sessionId(makeSessionId())
    {
        server.setMainThreadInvoker(
            [this](std::function<nlohmann::json()> task, std::chrono::milliseconds timeout)
            {
                return queue.invoke(std::move(task), timeout);
            });
    }

    McpService::~McpService()
    {
        stop();
    }

    bool McpService::start(uint16_t port, const std::string& token)
    {
        stop();
        queue.reset();

        http::HttpServer::Options options;
        options.port = port;

        // Armed before the listener accepts anything; re-armed below with the
        // real port when an ephemeral port (0) was requested.
        {
            std::lock_guard<std::mutex> lock(statusMutex);
            security = http::SecurityPolicy(port, token);
            sessionId = makeSessionId();
        }

        std::string error;
        bool ok = http.start(options, [this](const http::HttpRequest& r) { return handleHttp(r); }, error);

        std::lock_guard<std::mutex> lock(statusMutex);
        if (!ok)
        {
            lastError = error;
            vfLogWarning("[MCP] server failed to start on 127.0.0.1:{}: {}", port, error);
            return false;
        }

        lastError.clear();
        security = http::SecurityPolicy(http.boundPort(), token);
        vfLogInfo("[MCP] listening on http://127.0.0.1:{}{} ({} tools{})", http.boundPort(), endpointPath,
                  tools.size(), token.empty() ? "" : ", bearer token required");
        return true;
    }

    void McpService::stop()
    {
        // Fail pending main-thread work first so connection threads blocked in
        // invoke() return and HttpServer::stop() can join them.
        queue.shutdown();
        if (http.isRunning())
        {
            http.stop();
            vfLogInfo("[MCP] server stopped");
        }
    }

    void McpService::drain(std::chrono::milliseconds budget)
    {
        queue.drain(budget);
    }

    McpStatus McpService::status() const
    {
        McpStatus s;
        {
            std::lock_guard<std::mutex> lock(statusMutex);
            s.error = lastError;
        }
        if (http.isRunning())
        {
            s.state = McpStatus::State::Listening;
            s.port = http.boundPort();
        }
        else
        {
            s.state = s.error.empty() ? McpStatus::State::Off : McpStatus::State::Error;
        }
        s.clientName = server.clientName();
        s.lastTool = server.lastToolName();
        s.toolCalls = server.toolCallCount();
        s.toolErrors = server.toolErrorCount();
        s.connections = http.activeConnections();
        s.toolCount = tools.size();
        return s;
    }

    http::HttpResponse McpService::handleHttp(const http::HttpRequest& request)
    {
        http::SecurityPolicy policy;
        std::string session;
        {
            std::lock_guard<std::mutex> lock(statusMutex);
            policy = security;
            session = sessionId;
        }

        http::SecurityPolicy::Verdict verdict = policy.check(request);
        if (!verdict.allowed)
        {
            http::HttpResponse denied = http::HttpResponse::text(verdict.status, verdict.reason);
            if (verdict.status == 401)
            {
                denied.setHeader("WWW-Authenticate", "Bearer");
            }
            return denied;
        }

        if (request.path() != endpointPath)
        {
            return http::HttpResponse::text(404, "MCP endpoint is " + std::string(endpointPath));
        }

        if (request.method == "GET")
        {
            // No server-initiated SSE stream in P1 (spec: 405 when unsupported).
            return http::HttpResponse::empty(405).setHeader("Allow", "POST, DELETE");
        }
        if (request.method == "DELETE")
        {
            return http::HttpResponse::empty(204);
        }
        if (request.method != "POST")
        {
            return http::HttpResponse::empty(405).setHeader("Allow", "POST, DELETE");
        }

        if (auto contentType = request.header("content-type"); contentType && !isJsonContentType(*contentType))
        {
            return http::HttpResponse::text(415, "Content-Type must be application/json");
        }

        jsonrpc::ParseOutcome outcome = jsonrpc::parse(request.body);
        if (outcome.errorResponse)
        {
            return http::HttpResponse::json(400, outcome.errorResponse->dump());
        }

        const bool isInitialize = outcome.message->kind == jsonrpc::MessageKind::Request &&
                                  outcome.message->method == "initialize";

        std::optional<nlohmann::json> response = server.handle(*outcome.message);
        if (!response)
        {
            return http::HttpResponse::empty(202);
        }

        // error_handler_t::replace: tool output may carry invalid UTF-8 from
        // engine strings (paths, log lines); never let dump() throw here.
        http::HttpResponse out = http::HttpResponse::json(
            200, response->dump(-1, ' ', false, nlohmann::json::error_handler_t::replace));
        if (isInitialize)
        {
            out.setHeader("Mcp-Session-Id", session);
        }
        return out;
    }
}
