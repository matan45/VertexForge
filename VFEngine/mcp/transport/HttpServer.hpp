#pragma once

#include "HttpMessage.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace mcp::http
{
    // Blocking-socket HTTP/1.1 server bound to the loopback interface only.
    // One accept thread + one thread per connection (capped). Written against
    // winsock directly: mType's WinSocketServer binds INADDR_ANY, which would
    // expose the editor to the network.
    class HttpServer
    {
    public:
        using Handler = std::function<HttpResponse(const HttpRequest&)>;

        struct Options
        {
            uint16_t port = 7878;  // 0 = ephemeral (tests)
            int maxConnections = 4;
            std::chrono::milliseconds idleTimeout{60000};  // keep-alive idle before close
            std::size_t maxBodyBytes = 4u * 1024u * 1024u;
        };

        HttpServer();
        ~HttpServer();

        HttpServer(const HttpServer&) = delete;
        HttpServer& operator=(const HttpServer&) = delete;

        // Binds 127.0.0.1:<port> and starts accepting. Returns false (with a
        // reason in `error`) when the port is taken or winsock fails.
        bool start(const Options& options, Handler handler, std::string& error);

        // Closes the listener and every open connection, joins all threads.
        void stop();

        bool isRunning() const { return running.load(std::memory_order_acquire); }
        uint16_t boundPort() const { return port; }
        std::size_t activeConnections() const;

    private:
        struct Connection
        {
            uintptr_t socket = 0;
            std::thread thread;
            std::atomic<bool> finished{false};
        };

        void acceptLoop();
        void serveConnection(Connection& connection);
        void reapFinished();

        Options opts;
        Handler handler;
        uintptr_t listenSocket = 0;
        uint16_t port = 0;
        bool winsockStarted = false;
        std::atomic<bool> running{false};
        std::thread acceptThread;

        mutable std::mutex connectionsMutex;
        std::vector<std::unique_ptr<Connection>> connections;
    };
}
