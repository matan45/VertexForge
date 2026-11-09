#include "HttpMessage.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace mcp::http
{
    namespace
    {
        std::string toLower(std::string_view s)
        {
            std::string out(s);
            std::transform(out.begin(), out.end(), out.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }

        std::string_view trim(std::string_view s)
        {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
            {
                s.remove_prefix(1);
            }
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
            {
                s.remove_suffix(1);
            }
            return s;
        }

        bool containsToken(std::string_view headerValue, std::string_view token)
        {
            // Comma-separated, case-insensitive token match ("keep-alive, Upgrade").
            std::string lowered = toLower(headerValue);
            std::string wanted = toLower(token);
            std::size_t pos = 0;
            while (pos <= lowered.size())
            {
                std::size_t comma = lowered.find(',', pos);
                std::string_view part = trim(std::string_view(lowered).substr(
                    pos, comma == std::string::npos ? std::string::npos : comma - pos));
                if (part == wanted)
                {
                    return true;
                }
                if (comma == std::string::npos)
                {
                    break;
                }
                pos = comma + 1;
            }
            return false;
        }
    }

    std::optional<std::string_view> HttpRequest::header(std::string_view name) const
    {
        std::string wanted = toLower(name);
        for (const auto& [key, value] : headers)
        {
            if (key == wanted)
            {
                return std::string_view(value);
            }
        }
        return std::nullopt;
    }

    std::string_view HttpRequest::path() const
    {
        std::string_view t(target);
        std::size_t q = t.find('?');
        return q == std::string_view::npos ? t : t.substr(0, q);
    }

    bool HttpRequest::keepAlive() const
    {
        auto connection = header("connection");
        if (version == "HTTP/1.0")
        {
            return connection && containsToken(*connection, "keep-alive");
        }
        return !(connection && containsToken(*connection, "close"));
    }

    std::string_view reasonPhrase(int status)
    {
        switch (status)
        {
        case 200: return "OK";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 408: return "Request Timeout";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 415: return "Unsupported Media Type";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default: return "Unknown";
        }
    }

    HttpResponse HttpResponse::json(int status, std::string body)
    {
        HttpResponse r;
        r.status = status;
        r.body = std::move(body);
        r.setHeader("Content-Type", "application/json");
        return r;
    }

    HttpResponse HttpResponse::empty(int status)
    {
        HttpResponse r;
        r.status = status;
        return r;
    }

    HttpResponse HttpResponse::text(int status, std::string body)
    {
        HttpResponse r;
        r.status = status;
        r.body = std::move(body);
        r.setHeader("Content-Type", "text/plain; charset=utf-8");
        return r;
    }

    HttpResponse& HttpResponse::setHeader(std::string name, std::string value)
    {
        std::string lowered = toLower(name);
        for (auto& [key, existing] : headers)
        {
            if (toLower(key) == lowered)
            {
                existing = std::move(value);
                return *this;
            }
        }
        headers.emplace_back(std::move(name), std::move(value));
        return *this;
    }

    std::string HttpResponse::serialize(bool keepAlive) const
    {
        std::string out;
        out.reserve(128 + body.size());
        out += "HTTP/1.1 ";
        out += std::to_string(status);
        out += ' ';
        out += reasonPhrase(status);
        out += "\r\n";
        for (const auto& [key, value] : headers)
        {
            out += key;
            out += ": ";
            out += value;
            out += "\r\n";
        }
        // 204 must not carry a body or Content-Length.
        if (status != 204)
        {
            out += "Content-Length: ";
            out += std::to_string(body.size());
            out += "\r\n";
        }
        out += keepAlive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
        out += "\r\n";
        if (status != 204)
        {
            out += body;
        }
        return out;
    }

    std::string HttpResponse::serializeStreamHead() const
    {
        std::string out;
        out.reserve(256);
        out += "HTTP/1.1 ";
        out += std::to_string(status);
        out += ' ';
        out += reasonPhrase(status);
        out += "\r\n";
        for (const auto& [key, value] : headers)
        {
            std::string lowered = toLower(key);
            if (lowered == "content-type" || lowered == "content-length" || lowered == "transfer-encoding" ||
                lowered == "connection" || lowered == "cache-control")
            {
                continue;
            }
            out += key;
            out += ": ";
            out += value;
            out += "\r\n";
        }
        out += "Content-Type: text/event-stream\r\n";
        out += "Cache-Control: no-cache\r\n";
        out += "Transfer-Encoding: chunked\r\n";
        out += "Connection: close\r\n";
        // Stops a buffering reverse proxy from holding events back.
        out += "X-Accel-Buffering: no\r\n";
        out += "\r\n";
        return out;
    }

    HttpRequestParser::HttpRequestParser(std::size_t maxBodyBytes, std::size_t maxHeaderBytes)
        : maxBody(maxBodyBytes), maxHeader(maxHeaderBytes)
    {
    }

    void HttpRequestParser::feed(std::string_view data)
    {
        buffer.append(data.data(), data.size());
    }

    HttpRequestParser::State HttpRequestParser::fail(int status, std::string message)
    {
        errorCode = status;
        errorText = std::move(message);
        return State::Error;
    }

    HttpRequestParser::State HttpRequestParser::next()
    {
        if (errorCode != 0)
        {
            return State::Error;
        }

        // Tolerate stray CRLFs between pipelined requests (RFC 9112 §2.2).
        std::size_t lead = 0;
        while (lead + 1 < buffer.size() && buffer[lead] == '\r' && buffer[lead + 1] == '\n')
        {
            lead += 2;
        }
        if (lead > 0)
        {
            buffer.erase(0, lead);
        }

        std::size_t headerEnd = buffer.find("\r\n\r\n");
        if (headerEnd == std::string::npos)
        {
            if (buffer.size() > maxHeader)
            {
                return fail(431, "request headers too large");
            }
            return State::NeedMore;
        }
        if (headerEnd > maxHeader)
        {
            return fail(431, "request headers too large");
        }

        HttpRequest request;
        std::string_view head(buffer.data(), headerEnd);

        std::size_t lineEnd = head.find("\r\n");
        std::string_view requestLine = head.substr(0, lineEnd);
        std::size_t sp1 = requestLine.find(' ');
        std::size_t sp2 = sp1 == std::string_view::npos ? std::string_view::npos : requestLine.find(' ', sp1 + 1);
        if (sp1 == std::string_view::npos || sp2 == std::string_view::npos)
        {
            return fail(400, "malformed request line");
        }
        request.method = std::string(requestLine.substr(0, sp1));
        request.target = std::string(requestLine.substr(sp1 + 1, sp2 - sp1 - 1));
        request.version = std::string(requestLine.substr(sp2 + 1));
        if (request.method.empty() || request.target.empty() ||
            (request.version != "HTTP/1.1" && request.version != "HTTP/1.0"))
        {
            return fail(400, "malformed request line");
        }

        std::size_t pos = lineEnd == std::string_view::npos ? head.size() : lineEnd + 2;
        while (pos < head.size())
        {
            std::size_t eol = head.find("\r\n", pos);
            std::string_view line = head.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);
            pos = eol == std::string_view::npos ? head.size() : eol + 2;
            if (line.empty())
            {
                continue;
            }
            std::size_t colon = line.find(':');
            if (colon == std::string_view::npos || colon == 0)
            {
                return fail(400, "malformed header line");
            }
            request.headers.emplace_back(toLower(trim(line.substr(0, colon))),
                                         std::string(trim(line.substr(colon + 1))));
        }

        if (auto te = request.header("transfer-encoding"); te && !containsToken(*te, "identity"))
        {
            return fail(501, "chunked request bodies are not supported; send Content-Length");
        }

        std::size_t contentLength = 0;
        if (auto cl = request.header("content-length"))
        {
            std::string_view value = trim(*cl);
            auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), contentLength);
            if (ec != std::errc{} || ptr != value.data() + value.size())
            {
                return fail(400, "invalid Content-Length");
            }
        }
        if (contentLength > maxBody)
        {
            return fail(413, "request body exceeds " + std::to_string(maxBody) + " bytes");
        }

        const std::size_t total = headerEnd + 4 + contentLength;
        if (buffer.size() < total)
        {
            return State::NeedMore;
        }

        request.body = buffer.substr(headerEnd + 4, contentLength);
        buffer.erase(0, total);
        current = std::move(request);
        return State::Complete;
    }

    HttpRequest HttpRequestParser::take()
    {
        return std::move(current);
    }
}
