#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "HttpServer.hpp"

#include <algorithm>
#include <charconv>

namespace mcp::http
{
    namespace
    {
        SOCKET asSocket(uintptr_t s)
        {
            return static_cast<SOCKET>(s);
        }

        bool sendAll(SOCKET s, const std::string& data)
        {
            const char* ptr = data.data();
            std::size_t remaining = data.size();
            while (remaining > 0)
            {
                int chunk = static_cast<int>(std::min<std::size_t>(remaining, 1u << 20));
                int sent = ::send(s, ptr, chunk, 0);
                if (sent == SOCKET_ERROR || sent == 0)
                {
                    return false;
                }
                ptr += sent;
                remaining -= static_cast<std::size_t>(sent);
            }
            return true;
        }

        std::string lastSocketError(const char* what)
        {
            return std::string(what) + " failed (WSA error " + std::to_string(WSAGetLastError()) + ")";
        }

        // Chunked-body writer for a streamed response (VK-1652).
        class SocketStreamSink : public StreamSink
        {
        public:
            SocketStreamSink(SOCKET s, const std::atomic<bool>& runningFlag)
                : handle(s), serverRunning(runningFlag)
            {
            }

            bool write(std::string_view data) override
            {
                if (failed || !running())
                {
                    return false;
                }
                if (data.empty())
                {
                    return true;  // a zero-size chunk would terminate the body
                }

                char size[32];
                auto [end, ec] = std::to_chars(size, size + sizeof(size), data.size(), 16);
                std::string chunk;
                chunk.reserve(static_cast<std::size_t>(end - size) + data.size() + 4);
                chunk.append(size, end);
                chunk += "\r\n";
                chunk.append(data.data(), data.size());
                chunk += "\r\n";
                failed = !sendAll(handle, chunk);
                return !failed;
            }

            bool peerClosed() override
            {
                if (failed)
                {
                    return true;
                }
                fd_set readable;
                FD_ZERO(&readable);
                FD_SET(handle, &readable);
                timeval immediate{0, 0};
                int ready = ::select(0, &readable, nullptr, nullptr, &immediate);
                if (ready == SOCKET_ERROR)
                {
                    return true;
                }
                if (ready == 0)
                {
                    return false;
                }
                // Readable: 0 = orderly close, error = reset/shutdown. Stray bytes
                // from the client are left in place and ignored.
                char probe = 0;
                int peeked = ::recv(handle, &probe, 1, MSG_PEEK);
                return peeked <= 0;
            }

            bool running() const override
            {
                return serverRunning.load(std::memory_order_acquire);
            }

        private:
            SOCKET handle;
            const std::atomic<bool>& serverRunning;
            bool failed = false;
        };
    }

    HttpServer::HttpServer()
    {
        listenSocket = static_cast<uintptr_t>(INVALID_SOCKET);
    }

    HttpServer::~HttpServer()
    {
        stop();
    }

    bool HttpServer::start(const Options& options, Handler requestHandler, std::string& error)
    {
        if (running.load(std::memory_order_acquire))
        {
            error = "server already running";
            return false;
        }

        WSADATA wsaData{};
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            error = "WSAStartup failed";
            return false;
        }
        winsockStarted = true;

        SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET)
        {
            error = lastSocketError("socket()");
            WSACleanup();
            winsockStarted = false;
            return false;
        }

        // Exclusive: never share the port with another process (Windows lets a
        // second SO_REUSEADDR socket hijack a bound port otherwise).
        BOOL exclusive = TRUE;
        ::setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(options.port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1 only

        if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
        {
            int code = WSAGetLastError();
            error = code == WSAEADDRINUSE || code == WSAEACCES
                ? "port " + std::to_string(options.port) + " is already in use"
                : lastSocketError("bind()");
            ::closesocket(s);
            WSACleanup();
            winsockStarted = false;
            return false;
        }

        if (::listen(s, SOMAXCONN) == SOCKET_ERROR)
        {
            error = lastSocketError("listen()");
            ::closesocket(s);
            WSACleanup();
            winsockStarted = false;
            return false;
        }

        sockaddr_in bound{};
        int boundLen = sizeof(bound);
        ::getsockname(s, reinterpret_cast<sockaddr*>(&bound), &boundLen);
        port = ntohs(bound.sin_port);

        opts = options;
        handler = std::move(requestHandler);
        listenSocket = static_cast<uintptr_t>(s);
        running.store(true, std::memory_order_release);
        acceptThread = std::thread([this]() { acceptLoop(); });
        return true;
    }

    void HttpServer::stop()
    {
        if (!running.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }

        // Closing the listener unblocks accept().
        ::closesocket(asSocket(listenSocket));
        listenSocket = static_cast<uintptr_t>(INVALID_SOCKET);
        if (acceptThread.joinable())
        {
            acceptThread.join();
        }

        // Shut every connection down so blocking recv() calls return, then join.
        // A connection blocked inside the handler (waiting on the main thread)
        // returns once the MainThreadQueue is shut down by the owner first.
        std::vector<std::unique_ptr<Connection>> toJoin;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            for (auto& connection : connections)
            {
                ::shutdown(asSocket(connection->socket), SD_BOTH);
            }
            toJoin.swap(connections);
        }
        for (auto& connection : toJoin)
        {
            if (connection->thread.joinable())
            {
                connection->thread.join();
            }
            ::closesocket(asSocket(connection->socket));
        }

        if (winsockStarted)
        {
            WSACleanup();
            winsockStarted = false;
        }
    }

    std::size_t HttpServer::activeConnections() const
    {
        std::lock_guard<std::mutex> lock(connectionsMutex);
        return static_cast<std::size_t>(std::count_if(connections.begin(), connections.end(),
            [](const std::unique_ptr<Connection>& c) { return !c->finished.load(std::memory_order_acquire); }));
    }

    void HttpServer::reapFinished()
    {
        std::vector<std::unique_ptr<Connection>> done;
        {
            std::lock_guard<std::mutex> lock(connectionsMutex);
            auto split = std::stable_partition(connections.begin(), connections.end(),
                [](const std::unique_ptr<Connection>& c) { return !c->finished.load(std::memory_order_acquire); });
            for (auto it = split; it != connections.end(); ++it)
            {
                done.push_back(std::move(*it));
            }
            connections.erase(split, connections.end());
        }
        for (auto& connection : done)
        {
            if (connection->thread.joinable())
            {
                connection->thread.join();
            }
            ::closesocket(asSocket(connection->socket));
        }
    }

    void HttpServer::acceptLoop()
    {
        while (running.load(std::memory_order_acquire))
        {
            SOCKET client = ::accept(asSocket(listenSocket), nullptr, nullptr);
            if (client == INVALID_SOCKET)
            {
                if (!running.load(std::memory_order_acquire))
                {
                    break;  // stop() closed the listener
                }
                // Transient failure (WSAEMFILE, WSAENOBUFS, ...): back off instead of spinning.
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            reapFinished();

            if (activeConnections() >= static_cast<std::size_t>(opts.maxConnections))
            {
                sendAll(client, HttpResponse::text(503, "too many connections").serialize(false));
                ::closesocket(client);
                continue;
            }

            DWORD timeoutMs = static_cast<DWORD>(opts.idleTimeout.count());
            ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
            BOOL noDelay = TRUE;
            ::setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&noDelay), sizeof(noDelay));

            auto connection = std::make_unique<Connection>();
            connection->socket = static_cast<uintptr_t>(client);
            Connection* raw = connection.get();
            {
                std::lock_guard<std::mutex> lock(connectionsMutex);
                connections.push_back(std::move(connection));
            }
            raw->thread = std::thread([this, raw]() { serveConnection(*raw); });
        }
    }

    void HttpServer::serveConnection(Connection& connection)
    {
        // Nothing may escape this thread: an uncaught exception would terminate
        // the editor.
        SOCKET s = asSocket(connection.socket);
        try
        {
            HttpRequestParser parser(opts.maxBodyBytes);
            char buffer[16 * 1024];
            bool open = true;

            while (open && running.load(std::memory_order_acquire))
            {
                HttpRequestParser::State state = parser.next();
                if (state == HttpRequestParser::State::NeedMore)
                {
                    int received = ::recv(s, buffer, sizeof(buffer), 0);
                    if (received <= 0)
                    {
                        break;  // closed, idle timeout, or shutdown by stop()
                    }
                    parser.feed(std::string_view(buffer, static_cast<std::size_t>(received)));
                    continue;
                }

                if (state == HttpRequestParser::State::Error)
                {
                    sendAll(s, HttpResponse::text(parser.errorStatus(), parser.errorMessage()).serialize(false));
                    break;
                }

                HttpRequest request = parser.take();
                bool keepAlive = request.keepAlive();
                HttpResponse response;
                try
                {
                    response = handler(request);
                }
                catch (const std::exception& e)
                {
                    response = HttpResponse::text(500, e.what());
                }
                catch (...)
                {
                    response = HttpResponse::text(500, "internal error");
                }

                if (response.stream)
                {
                    // Streamed body: the connection is dedicated to it and closed
                    // afterwards (keep-alive does not apply).
                    if (!sendAll(s, response.serializeStreamHead()))
                    {
                        break;
                    }
                    // A client that stops reading fails the stream after 5 s
                    // instead of blocking this thread forever.
                    DWORD sendTimeoutMs = 5000;
                    ::setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendTimeoutMs),
                                 sizeof(sendTimeoutMs));
                    SocketStreamSink sink(s, running);
                    try
                    {
                        response.stream(sink);
                    }
                    catch (...)
                    {
                    }
                    sendAll(s, "0\r\n\r\n");
                    break;
                }

                if (!sendAll(s, response.serialize(keepAlive)))
                {
                    break;
                }
                open = keepAlive;
            }
        }
        catch (...)
        {
        }

        // The handle is closed by the owner after join (reapFinished / stop), so
        // stop() can never shutdown() a handle that was already recycled.
        ::shutdown(s, SD_SEND);
        connection.finished.store(true, std::memory_order_release);
    }
}
