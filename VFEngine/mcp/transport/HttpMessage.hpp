#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Minimal HTTP/1.1 message handling for the MCP Streamable HTTP transport.
// Requests: Content-Length bodies only (MCP clients POST fixed-size JSON).
// Responses: Content-Length, or a chunked text/event-stream (VK-1652).
namespace mcp::http
{
    // Write side of a streamed (chunked) response. Implemented over the socket by
    // HttpServer; tests substitute their own. Used from the connection thread only.
    class StreamSink
    {
    public:
        virtual ~StreamSink() = default;

        // Sends `data` as one HTTP/1.1 chunk (an empty write sends nothing).
        // False on a send error / timeout or once the server is stopping.
        virtual bool write(std::string_view data) = 0;

        // Non-blocking: true when the peer closed the connection or it failed.
        virtual bool peerClosed() = 0;

        // False once the server is stopping.
        virtual bool running() const = 0;
    };

    struct HttpRequest
    {
        std::string method;
        std::string target;   // path + optional query, e.g. "/mcp"
        std::string version;  // "HTTP/1.1"
        std::vector<std::pair<std::string, std::string>> headers;  // names lower-cased
        std::string body;

        // Case-insensitive lookup (names are stored lower-cased).
        std::optional<std::string_view> header(std::string_view name) const;

        // Path component of the target (query string stripped).
        std::string_view path() const;

        bool keepAlive() const;
    };

    struct HttpResponse
    {
        int status = 200;
        std::vector<std::pair<std::string, std::string>> headers;
        std::string body;

        // When set the response is streamed: HttpServer sends serializeStreamHead(),
        // runs this on the connection thread until it returns, terminates the chunked
        // body and closes the connection. `body` is ignored.
        std::function<void(StreamSink&)> stream;

        static HttpResponse json(int status, std::string body);
        static HttpResponse empty(int status);
        static HttpResponse text(int status, std::string body);

        HttpResponse& setHeader(std::string name, std::string value);

        std::string serialize(bool keepAlive) const;

        // Status line + headers of a text/event-stream response with a chunked
        // body (no Content-Length, Connection: close). Headers that the stream
        // framing owns (Content-Type, Content-Length, Transfer-Encoding,
        // Connection, Cache-Control) are not copied from `headers`.
        std::string serializeStreamHead() const;
    };

    std::string_view reasonPhrase(int status);

    // Incremental request parser. feed() bytes as they arrive, then call next()
    // until it stops returning Complete (several pipelined requests may be
    // buffered).
    class HttpRequestParser
    {
    public:
        enum class State
        {
            NeedMore,
            Complete,
            Error
        };

        explicit HttpRequestParser(std::size_t maxBodyBytes = 4u * 1024u * 1024u,
                                   std::size_t maxHeaderBytes = 64u * 1024u);

        void feed(std::string_view data);

        // Tries to parse one request from the buffered bytes.
        State next();

        // The request produced by the last Complete.
        HttpRequest take();

        // HTTP status to answer with after Error (400 / 413 / 431 / 501).
        int errorStatus() const { return errorCode; }
        const std::string& errorMessage() const { return errorText; }

        std::size_t bufferedBytes() const { return buffer.size(); }

    private:
        State fail(int status, std::string message);

        std::size_t maxBody;
        std::size_t maxHeader;
        std::string buffer;
        HttpRequest current;
        int errorCode = 0;
        std::string errorText;
    };
}
