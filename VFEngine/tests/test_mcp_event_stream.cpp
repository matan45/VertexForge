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
#include "transport/EventStreamHub.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace
{
    // Records writes; failure / peer close / server stop are switchable.
    class FakeSink : public mcp::http::StreamSink
    {
    public:
        bool write(std::string_view data) override
        {
            if (failWrites.load())
            {
                return false;
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                written.append(data.data(), data.size());
            }
            changed.notify_all();
            return true;
        }

        bool peerClosed() override { return peerGone.load(); }
        bool running() const override { return live.load(); }

        std::string text() const
        {
            std::lock_guard<std::mutex> lock(mutex);
            return written;
        }

        // True once the written text contains `needle` (within `timeout`).
        bool waitFor(const std::string& needle, std::chrono::milliseconds timeout = 5000ms)
        {
            std::unique_lock<std::mutex> lock(mutex);
            return changed.wait_for(lock, timeout, [&]() { return written.find(needle) != std::string::npos; });
        }

        std::atomic<bool> failWrites{false};
        std::atomic<bool> peerGone{false};
        std::atomic<bool> live{true};

    private:
        mutable std::mutex mutex;
        std::condition_variable changed;
        std::string written;
    };

    using Hub = mcp::http::EventStreamHub;

    // Runs hub.serve() on a worker thread. On scope exit it closes the hub and
    // stops the sink, so a failing test cannot leave the worker waiting forever.
    class ServeTask
    {
    public:
        ServeTask(Hub& targetHub, std::shared_ptr<Hub::Stream> stream, FakeSink& targetSink,
                  std::chrono::milliseconds keepalive = 60000ms)
            : hub(targetHub), sink(targetSink)
        {
            done = std::async(std::launch::async, [this, stream = std::move(stream), keepalive]()
            {
                hub.serve(stream, sink, keepalive);
            });
        }

        ~ServeTask()
        {
            sink.live = false;
            hub.close();
            done.wait();
        }

        ServeTask(const ServeTask&) = delete;
        ServeTask& operator=(const ServeTask&) = delete;

        bool finishes(std::chrono::milliseconds timeout = 5000ms)
        {
            return done.wait_for(timeout) == std::future_status::ready;
        }

    private:
        Hub& hub;
        FakeSink& sink;
        std::future<void> done;
    };
}

TEST_CASE("mcp event stream: event formatting")
{
    CHECK(Hub::formatEvent(7, R"({"a":1})") == "event: message\nid: 7\ndata: {\"a\":1}\n\n");
    CHECK(Hub::formatEvent(1, "{\r\n\"a\":1\n}") == "event: message\nid: 1\ndata: {\ndata: \"a\":1\ndata: }\n\n");
}

TEST_CASE("mcp event stream: at most two streams, a third evicts the oldest")
{
    Hub hub;
    auto first = hub.open();
    auto second = hub.open();
    REQUIRE(first);
    REQUIRE(second);
    CHECK(hub.streamCount() == 2);

    auto third = hub.open();
    REQUIRE(third);
    CHECK(hub.streamCount() == 2);
    CHECK(first->closed);
    CHECK_FALSE(second->closed);
    CHECK_FALSE(third->closed);

    // The evicted stream's serve() returns at once.
    FakeSink sink;
    ServeTask served(hub, first, sink);
    CHECK(served.finishes());
    CHECK(hub.streamCount() == 2);
}

TEST_CASE("mcp event stream: an evicted stream that is being served stops")
{
    Hub hub;
    auto first = hub.open();
    FakeSink sink;
    ServeTask served(hub, first, sink);

    hub.open();
    hub.open();
    CHECK(served.finishes());
    CHECK(hub.streamCount() == 2);
}

TEST_CASE("mcp event stream: broadcast reaches every stream with increasing ids")
{
    Hub hub;
    hub.broadcast(R"({"dropped":true})");  // no stream yet

    auto a = hub.open();
    auto b = hub.open();
    FakeSink sinkA;
    FakeSink sinkB;
    ServeTask servedA(hub, a, sinkA);
    ServeTask servedB(hub, b, sinkB);

    hub.broadcast(R"({"n":1})");
    hub.broadcast(R"({"n":2})");

    REQUIRE(sinkA.waitFor("data: {\"n\":2}"));
    REQUIRE(sinkB.waitFor("data: {\"n\":2}"));
    for (FakeSink* sink : {&sinkA, &sinkB})
    {
        std::string text = sink->text();
        std::size_t one = text.find("event: message\nid: 1\ndata: {\"n\":1}\n\n");
        std::size_t two = text.find("event: message\nid: 2\ndata: {\"n\":2}\n\n");
        CHECK(one != std::string::npos);
        CHECK(two != std::string::npos);
        CHECK(one < two);
        CHECK(text.find("dropped") == std::string::npos);
    }

    hub.close();
    CHECK(servedA.finishes());
    CHECK(servedB.finishes());
    CHECK(hub.streamCount() == 0);
}

TEST_CASE("mcp event stream: close wakes a waiting stream, reset reopens")
{
    Hub hub;
    auto stream = hub.open();
    FakeSink sink;
    ServeTask served(hub, stream, sink, 60000ms);

    hub.close();
    CHECK(served.finishes());
    CHECK_FALSE(hub.isOpen());
    CHECK(hub.open() == nullptr);
    CHECK(hub.streamCount() == 0);

    hub.reset();
    CHECK(hub.isOpen());
    auto again = hub.open();
    CHECK(again != nullptr);
    CHECK(hub.streamCount() == 1);
}

TEST_CASE("mcp event stream: keepalive is written when idle and a closed peer ends the stream")
{
    Hub hub;
    auto stream = hub.open();
    FakeSink sink;
    ServeTask served(hub, stream, sink, 20ms);

    REQUIRE(sink.waitFor(": keepalive\n\n"));
    sink.peerGone = true;
    CHECK(served.finishes());
    CHECK(hub.streamCount() == 0);
}

TEST_CASE("mcp event stream: a failed write ends the stream and removes it")
{
    Hub hub;
    auto stream = hub.open();
    FakeSink sink;
    sink.failWrites = true;
    ServeTask served(hub, stream, sink);

    hub.broadcast(R"({"n":1})");
    CHECK(served.finishes());
    CHECK(hub.streamCount() == 0);
    CHECK(stream->closed);
}

TEST_CASE("mcp event stream: a stopping server ends the stream")
{
    Hub hub;
    auto stream = hub.open();
    FakeSink sink;
    sink.live = false;
    ServeTask served(hub, stream, sink);
    CHECK(served.finishes());
    CHECK(hub.streamCount() == 0);
}

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

    SOCKET connectLoopback(uint16_t port)
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
        return s;
    }

    void sendText(SOCKET s, const std::string& text)
    {
        REQUIRE(::send(s, text.data(), static_cast<int>(text.size()), 0) == static_cast<int>(text.size()));
    }

    // Reads until `needle` is buffered, the peer closes or the 5 s timeout hits.
    bool readUntil(SOCKET s, std::string& buffer, const std::string& needle)
    {
        char chunk[4096];
        while (buffer.find(needle) == std::string::npos)
        {
            int n = ::recv(s, chunk, sizeof(chunk), 0);
            if (n <= 0)
            {
                return false;
            }
            buffer.append(chunk, static_cast<std::size_t>(n));
        }
        return true;
    }

    std::string getRequest(uint16_t port, const std::string& extraHeaders)
    {
        return "GET /mcp HTTP/1.1\r\n"
               "Host: 127.0.0.1:" + std::to_string(port) + "\r\n" + extraHeaders + "\r\n";
    }
}

TEST_CASE("mcp loopback: GET /mcp streams a broadcast notification")
{
    WinsockScope winsock;
    mcp::McpService service;
    REQUIRE(service.start(0, ""));
    const uint16_t port = service.status().port;
    REQUIRE(port != 0);

    SUBCASE("without Accept: text/event-stream is 406")
    {
        SOCKET s = connectLoopback(port);
        sendText(s, getRequest(port, "Accept: application/json\r\nConnection: close\r\n"));
        std::string response;
        readUntil(s, response, "\r\n\r\n");
        ::closesocket(s);
        CHECK(response.rfind("HTTP/1.1 406", 0) == 0);
    }

    SUBCASE("stream head, event, then a prompt stop")
    {
        SOCKET s = connectLoopback(port);
        sendText(s, getRequest(port, "Accept: text/event-stream\r\nLast-Event-ID: 41\r\n"));

        std::string received;
        REQUIRE(readUntil(s, received, "\r\n\r\n"));
        std::string head = received.substr(0, received.find("\r\n\r\n") + 4);
        CHECK(head.rfind("HTTP/1.1 200", 0) == 0);
        CHECK(head.find("Content-Type: text/event-stream\r\n") != std::string::npos);
        CHECK(head.find("Transfer-Encoding: chunked\r\n") != std::string::npos);
        CHECK(head.find("Content-Length") == std::string::npos);

        // The stream registers right after the head is sent.
        auto deadline = std::chrono::steady_clock::now() + 5s;
        while (service.status().eventStreams != 1 && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(5ms);
        }
        REQUIRE(service.status().eventStreams == 1);

        service.broadcastNotification(R"({"jsonrpc":"2.0","method":"notifications/tools/list_changed"})");
        REQUIRE(readUntil(s, received, "notifications/tools/list_changed"));
        CHECK(received.find("event: message\nid: ") != std::string::npos);

        auto stopStart = std::chrono::steady_clock::now();
        service.stop();
        CHECK(std::chrono::steady_clock::now() - stopStart < 3s);
        CHECK_FALSE(service.isRunning());
        CHECK(service.status().eventStreams == 0);

        // The connection closes. Only the close is checked, well inside the 5 s
        // receive timeout: the "0\r\n\r\n" terminator may lose the race with
        // HttpServer::stop()'s shutdown().
        auto closeStart = std::chrono::steady_clock::now();
        CHECK_FALSE(readUntil(s, received, "never sent"));
        CHECK(std::chrono::steady_clock::now() - closeStart < 3s);
        ::closesocket(s);
    }

    service.stop();
}
