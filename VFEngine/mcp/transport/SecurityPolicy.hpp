#pragma once

#include "HttpMessage.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace mcp::http
{
    // Request admission for the localhost MCP endpoint (VK-1650).
    //  - Origin: browsers attach it to cross-site fetches. Rejecting non-local
    //    origins blocks DNS-rebinding / drive-by pages from driving the editor.
    //  - Host: must name the loopback interface on our port (rebinding defence).
    //  - Token: optional shared secret, "Authorization: Bearer <token>".
    class SecurityPolicy
    {
    public:
        struct Verdict
        {
            bool allowed = true;
            int status = 200;
            std::string reason;
        };

        SecurityPolicy() = default;
        SecurityPolicy(uint16_t listenPort, std::string bearerToken)
            : port(listenPort), token(std::move(bearerToken))
        {
        }

        Verdict check(const HttpRequest& request) const
        {
            if (auto origin = request.header("origin"))
            {
                if (!isLoopbackOrigin(*origin))
                {
                    return deny(403, "origin not allowed");
                }
            }

            auto host = request.header("host");
            if (!host || !isLoopbackHost(*host))
            {
                return deny(403, "host not allowed");
            }

            if (!token.empty())
            {
                auto auth = request.header("authorization");
                constexpr std::string_view prefix = "Bearer ";
                if (!auth || auth->size() <= prefix.size() || !equalsIgnoreCase(auth->substr(0, prefix.size()), prefix) ||
                    !constantTimeEquals(auth->substr(prefix.size()), token))
                {
                    return deny(401, "missing or invalid bearer token");
                }
            }
            return {};
        }

        // "localhost", "127.0.0.1" or "[::1]", with ":<port>" (port may be
        // omitted only when we listen on 80).
        bool isLoopbackHost(std::string_view host) const
        {
            std::string_view name = host;
            std::string_view portPart;
            if (!host.empty() && host.front() == '[')
            {
                std::size_t close = host.find(']');
                if (close == std::string_view::npos)
                {
                    return false;
                }
                name = host.substr(0, close + 1);
                std::string_view rest = host.substr(close + 1);
                if (!rest.empty())
                {
                    if (rest.front() != ':')
                    {
                        return false;
                    }
                    portPart = rest.substr(1);
                }
            }
            else if (std::size_t colon = host.rfind(':'); colon != std::string_view::npos)
            {
                name = host.substr(0, colon);
                portPart = host.substr(colon + 1);
            }

            if (!(equalsIgnoreCase(name, "localhost") || name == "127.0.0.1" || name == "[::1]"))
            {
                return false;
            }
            if (portPart.empty())
            {
                return port == 80;
            }
            return portPart == std::to_string(port);
        }

        // Any port is accepted for the origin: local dev tools (MCP Inspector)
        // serve their UI from a different port than ours.
        static bool isLoopbackOrigin(std::string_view origin)
        {
            std::string_view rest;
            if (startsWithIgnoreCase(origin, "http://"))
            {
                rest = origin.substr(7);
            }
            else if (startsWithIgnoreCase(origin, "https://"))
            {
                rest = origin.substr(8);
            }
            else
            {
                return false;  // includes the opaque "null" origin (file://, sandboxed iframes)
            }

            std::string_view hostName = rest;
            if (!rest.empty() && rest.front() == '[')
            {
                std::size_t close = rest.find(']');
                if (close == std::string_view::npos)
                {
                    return false;
                }
                hostName = rest.substr(0, close + 1);
                rest = rest.substr(close + 1);
            }
            else
            {
                std::size_t end = rest.find_first_of(":/");
                hostName = rest.substr(0, end);
                rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end);
            }

            if (!(equalsIgnoreCase(hostName, "localhost") || hostName == "127.0.0.1" || hostName == "[::1]"))
            {
                return false;
            }
            // Only an optional ":<digits>" may follow (an Origin carries no path).
            if (rest.empty())
            {
                return true;
            }
            if (rest.front() != ':' || rest.size() == 1)
            {
                return false;
            }
            for (char c : rest.substr(1))
            {
                if (c < '0' || c > '9')
                {
                    return false;
                }
            }
            return true;
        }

        static bool constantTimeEquals(std::string_view a, std::string_view b)
        {
            // Length leaks, contents do not.
            unsigned char diff = static_cast<unsigned char>(a.size() != b.size());
            const std::size_t n = a.size() < b.size() ? a.size() : b.size();
            for (std::size_t i = 0; i < n; ++i)
            {
                diff |= static_cast<unsigned char>(a[i] ^ b[i]);
            }
            return diff == 0;
        }

    private:
        static Verdict deny(int status, std::string reason)
        {
            return Verdict{false, status, std::move(reason)};
        }

        static char lower(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        static bool equalsIgnoreCase(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                if (lower(a[i]) != lower(b[i]))
                {
                    return false;
                }
            }
            return true;
        }

        static bool startsWithIgnoreCase(std::string_view s, std::string_view prefix)
        {
            return s.size() >= prefix.size() && equalsIgnoreCase(s.substr(0, prefix.size()), prefix);
        }

        uint16_t port = 0;
        std::string token;
    };
}
