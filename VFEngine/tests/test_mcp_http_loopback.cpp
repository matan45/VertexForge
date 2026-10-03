#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <doctest.h>

#include "McpService.hpp"
#include "protocol/ArgReader.hpp"

#include <atomic>
#include <future>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace
{
    struct WinsockScope
    {
        WinsockScope()
        {
            WSADATA data{};
            WSAStartup(MAKEWORD(2, 2), &data);
        }
        ~WinsockScope() { WSACleanup(); }
    };

    // Sends one raw HTTP request and reads until the peer closes the connection
    // (requests use "Connection: close").
    std::string roundTrip(uint16_t port, const std::string& request)
    {
        SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        REQUIRE(s != INVALID_SOCKET);
        DWORD timeout = 5000;
        ::setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        REQUIRE(::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
        REQUIRE(::send(s, request.data(), static_cast<int>(request.size()), 0) == static_cast<int>(request.size()));

        std::string response;
        char buffer[4096];
        for (;;)
        {
            int n = ::recv(s, buffer, sizeof(buffer), 0);
            if (n <= 0)
            {
                break;
            }
            response.append(buffer, static_cast<std::size_t>(n));
        }
        ::closesocket(s);
        return response;
    }

    std::string post(uint16_t port, const std::string& body, const std::string& extraHeaders = {})
    {
        return roundTrip(port,
            "POST /mcp HTTP/1.1\r\n"
            "Host: 127.0.0.1:" + std::to_string(port) + "\r\n"
            "Content-Type: application/json\r\n"
            "Accept: application/json, text/event-stream\r\n"
            "Connection: close\r\n" + extraHeaders +
            "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
    }

    nlohmann::json bodyOf(const std::string& response)
    {
        std::size_t split = response.find("\r\n\r\n");
        REQUIRE(split != std::string::npos);
        return nlohmann::json::parse(response.substr(split + 4));
    }
}

TEST_CASE("mcp loopback: initialize and a main-thread tool call over real sockets")
{
    WinsockScope winsock;
    mcp::McpService service;

    mcp::ToolDef add;
    add.name = "add";
    add.description = "Adds a and b on the main thread";
    add.inputSchema = mcp::schema::object({{"a", mcp::schema::number("a")}, {"b", mcp::schema::number("b")}}, {"a", "b"});
    add.handler = [](const nlohmann::json& args)
    {
        mcp::ArgReader reader(args);
        return mcp::ToolResult::ok({{"sum", reader.requireNumber("a") + reader.requireNumber("b")}});
    };
    service.registry().add(add);

    REQUIRE(service.start(0, ""));
    const uint16_t port = service.status().port;
    REQUIRE(port != 0);

    // Stand-in for the editor frame loop.
    std::atomic<bool> pumping{true};
    std::thread mainLoop([&]()
    {
        while (pumping)
        {
            service.drain();
            std::this_thread::sleep_for(1ms);
        }
    });

    std::string init = post(port,
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"doctest","version":"1"}}})");
    CHECK(init.rfind("HTTP/1.1 200", 0) == 0);
    CHECK(init.find("Mcp-Session-Id: ") != std::string::npos);
    CHECK(bodyOf(init)["result"]["protocolVersion"] == "2025-06-18");

    std::string note = post(port, R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    CHECK(note.rfind("HTTP/1.1 202", 0) == 0);

    std::string called = post(port,
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"add","arguments":{"a":2,"b":3}}})");
    CHECK(bodyOf(called)["result"]["structuredContent"]["sum"] == 5.0);

    std::string forbidden = post(port, R"({"jsonrpc":"2.0","id":3,"method":"ping"})", "Origin: http://evil.example\r\n");
    CHECK(forbidden.rfind("HTTP/1.1 403", 0) == 0);

    CHECK(service.status().clientName == "doctest 1");

    pumping = false;
    mainLoop.join();
    service.stop();
    CHECK_FALSE(service.isRunning());
}

TEST_CASE("mcp loopback: token is enforced and the port cannot be shared")
{
    WinsockScope winsock;
    mcp::McpService service;
    REQUIRE(service.start(0, "tok"));
    const uint16_t port = service.status().port;

    std::string denied = post(port, R"({"jsonrpc":"2.0","id":1,"method":"ping"})");
    CHECK(denied.rfind("HTTP/1.1 401", 0) == 0);

    // ping needs no main-thread hop, so no pump is required.
    std::string allowed = post(port, R"({"jsonrpc":"2.0","id":1,"method":"ping"})", "Authorization: Bearer tok\r\n");
    CHECK(allowed.rfind("HTTP/1.1 200", 0) == 0);

    mcp::McpService second;
    CHECK_FALSE(second.start(port, ""));
    CHECK(second.status().state == mcp::McpStatus::State::Error);

    service.stop();
}
