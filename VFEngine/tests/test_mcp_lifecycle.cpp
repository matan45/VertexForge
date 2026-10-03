#include <doctest.h>

#include "McpService.hpp"
#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/McpServer.hpp"
#include "protocol/ToolRegistry.hpp"

#include <stdexcept>

namespace
{
    mcp::ToolRegistry makeRegistry()
    {
        mcp::ToolRegistry registry;

        mcp::ToolDef echo;
        echo.name = "echo";
        echo.description = "Echo the text argument";
        echo.inputSchema = mcp::schema::object({{"text", mcp::schema::string("text")}}, {"text"});
        echo.readOnly = true;
        echo.handler = [](const nlohmann::json& args)
        {
            mcp::ArgReader reader(args);
            return mcp::ToolResult::ok({{"echo", reader.requireString("text")}});
        };
        registry.add(echo);

        mcp::ToolDef boom;
        boom.name = "boom";
        boom.description = "Throws";
        boom.handler = [](const nlohmann::json&) -> mcp::ToolResult
        {
            throw std::runtime_error("No handler registered for command");
        };
        registry.add(boom);
        return registry;
    }

    nlohmann::json call(mcp::McpServer& server, const nlohmann::json& request)
    {
        auto response = server.handleBody(request.dump());
        REQUIRE(response);
        return *response;
    }

    nlohmann::json toolCall(int id, const std::string& name, nlohmann::json args)
    {
        return {
            {"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"},
            {"params", {{"name", name}, {"arguments", std::move(args)}}}
        };
    }
}

TEST_CASE("mcp lifecycle: initialize negotiates the protocol version")
{
    mcp::ToolRegistry registry = makeRegistry();
    mcp::McpServer server(registry, {});

    SUBCASE("supported version is echoed")
    {
        nlohmann::json r = call(server, {
            {"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
            {"params", {{"protocolVersion", "2025-03-26"}, {"capabilities", nlohmann::json::object()},
                        {"clientInfo", {{"name", "claude-code"}, {"version", "2.0"}}}}}
        });
        CHECK(r["result"]["protocolVersion"] == "2025-03-26");
        CHECK(r["result"]["capabilities"]["tools"]["listChanged"] == false);
        CHECK(r["result"]["serverInfo"]["name"] == "vertexforge-editor");
        CHECK(server.clientName() == "claude-code 2.0");
    }
    SUBCASE("unknown version falls back to latest")
    {
        nlohmann::json r = call(server, {
            {"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
            {"params", {{"protocolVersion", "1999-01-01"}}}
        });
        CHECK(r["result"]["protocolVersion"] == mcp::McpServer::latestProtocolVersion);
    }
}

TEST_CASE("mcp lifecycle: ping, notifications and unknown methods")
{
    mcp::ToolRegistry registry = makeRegistry();
    mcp::McpServer server(registry, {});

    nlohmann::json pong = call(server, {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "ping"}});
    CHECK(pong["result"].is_object());
    CHECK(pong["result"].empty());

    CHECK_FALSE(server.handleBody(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));

    nlohmann::json missing = call(server, {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "resources/list"}});
    CHECK(missing["error"]["code"] == mcp::jsonrpc::errc::methodNotFound);
}

TEST_CASE("mcp lifecycle: tools/list exposes schema and annotations")
{
    mcp::ToolRegistry registry = makeRegistry();
    mcp::McpServer server(registry, {});

    nlohmann::json r = call(server, {{"jsonrpc", "2.0"}, {"id", 4}, {"method", "tools/list"}});
    const nlohmann::json& tools = r["result"]["tools"];
    REQUIRE(tools.size() == 2);
    CHECK(tools[0]["name"] == "echo");
    CHECK(tools[0]["inputSchema"]["type"] == "object");
    CHECK(tools[0]["inputSchema"]["required"][0] == "text");
    CHECK(tools[0]["annotations"]["readOnlyHint"] == true);
}

TEST_CASE("mcp lifecycle: tools/call results and in-band errors")
{
    mcp::ToolRegistry registry = makeRegistry();
    mcp::McpServer server(registry, {});

    SUBCASE("success returns text + structuredContent")
    {
        nlohmann::json r = call(server, toolCall(5, "echo", {{"text", "hi"}}));
        CHECK(r["result"]["isError"] == false);
        CHECK(r["result"]["structuredContent"]["echo"] == "hi");
        CHECK(r["result"]["content"][0]["type"] == "text");
    }
    SUBCASE("argument errors are tool errors, not protocol errors")
    {
        nlohmann::json r = call(server, toolCall(6, "echo", nlohmann::json::object()));
        REQUIRE(r.contains("result"));
        CHECK(r["result"]["isError"] == true);
        CHECK(r["result"]["content"][0]["text"].get<std::string>().find("text") != std::string::npos);
    }
    SUBCASE("handler exceptions become isError")
    {
        nlohmann::json r = call(server, toolCall(7, "boom", nlohmann::json::object()));
        CHECK(r["result"]["isError"] == true);
        CHECK(server.toolErrorCount() == 1);
    }
    SUBCASE("unknown tool is invalid params")
    {
        nlohmann::json r = call(server, toolCall(8, "nope", nlohmann::json::object()));
        CHECK(r["error"]["code"] == mcp::jsonrpc::errc::invalidParams);
    }
    SUBCASE("non-object arguments rejected")
    {
        nlohmann::json r = call(server, toolCall(9, "echo", nlohmann::json::array()));
        CHECK(r["error"]["code"] == mcp::jsonrpc::errc::invalidParams);
    }
}

TEST_CASE("mcp lifecycle: main-affinity tools go through the invoker")
{
    mcp::ToolRegistry registry = makeRegistry();
    mcp::McpServer server(registry, {});

    int invoked = 0;
    server.setMainThreadInvoker([&](std::function<nlohmann::json()> task, std::chrono::milliseconds)
    {
        ++invoked;
        return task();
    });
    nlohmann::json ok = call(server, toolCall(10, "echo", {{"text", "x"}}));
    CHECK(invoked == 1);
    CHECK(ok["result"]["isError"] == false);

    server.setMainThreadInvoker([](std::function<nlohmann::json()>, std::chrono::milliseconds) -> nlohmann::json
    {
        throw mcp::MainThreadTimeout("busy");
    });
    nlohmann::json timedOut = call(server, toolCall(11, "echo", {{"text", "x"}}));
    CHECK(timedOut["error"]["code"] == mcp::jsonrpc::errc::mainThreadTimeout);
}

TEST_CASE("mcp lifecycle: HTTP routing through McpService")
{
    mcp::McpService service;  // not started: security policy port 0

    auto makeRequest = [](std::string method, std::string body)
    {
        mcp::http::HttpRequest request;
        request.method = std::move(method);
        request.target = "/mcp";
        request.version = "HTTP/1.1";
        request.headers = {{"host", "127.0.0.1:0"}, {"content-type", "application/json"}};
        request.body = std::move(body);
        return request;
    };

    SUBCASE("initialize returns JSON and a session id")
    {
        auto response = service.handleHttp(makeRequest("POST",
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})"));
        CHECK(response.status == 200);
        bool hasSession = false;
        for (const auto& [name, value] : response.headers)
        {
            hasSession |= name == "Mcp-Session-Id" && !value.empty();
        }
        CHECK(hasSession);
    }
    SUBCASE("notification is 202")
    {
        auto response = service.handleHttp(makeRequest("POST", R"({"jsonrpc":"2.0","method":"notifications/initialized"})"));
        CHECK(response.status == 202);
    }
    SUBCASE("GET is 405, DELETE is 204, other path is 404")
    {
        CHECK(service.handleHttp(makeRequest("GET", "")).status == 405);
        CHECK(service.handleHttp(makeRequest("DELETE", "")).status == 204);
        auto other = makeRequest("POST", "{}");
        other.target = "/other";
        CHECK(service.handleHttp(other).status == 404);
    }
    SUBCASE("parse error is 400 with a JSON-RPC body")
    {
        auto response = service.handleHttp(makeRequest("POST", "{oops"));
        CHECK(response.status == 400);
        CHECK(nlohmann::json::parse(response.body)["error"]["code"] == mcp::jsonrpc::errc::parseError);
    }
    SUBCASE("foreign origin is 403")
    {
        auto request = makeRequest("POST", R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
        request.headers.emplace_back("origin", "http://evil.example");
        CHECK(service.handleHttp(request).status == 403);
    }
    SUBCASE("wrong content type is 415")
    {
        auto request = makeRequest("POST", R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
        request.headers[1].second = "text/plain";
        CHECK(service.handleHttp(request).status == 415);
    }
}
