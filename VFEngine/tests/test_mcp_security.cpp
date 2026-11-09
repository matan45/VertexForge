#include <doctest.h>

#include "transport/SecurityPolicy.hpp"

using mcp::http::HttpRequest;
using mcp::http::SecurityPolicy;

namespace
{
    HttpRequest makeRequest(std::vector<std::pair<std::string, std::string>> headers)
    {
        HttpRequest request;
        request.method = "POST";
        request.target = "/mcp";
        request.version = "HTTP/1.1";
        request.headers = std::move(headers);
        return request;
    }
}

TEST_CASE("mcp security: host must be loopback on our port")
{
    SecurityPolicy policy(7878, "");
    CHECK(policy.check(makeRequest({{"host", "127.0.0.1:7878"}})).allowed);
    CHECK(policy.check(makeRequest({{"host", "localhost:7878"}})).allowed);
    CHECK(policy.check(makeRequest({{"host", "LOCALHOST:7878"}})).allowed);
    CHECK(policy.check(makeRequest({{"host", "[::1]:7878"}})).allowed);

    CHECK_FALSE(policy.check(makeRequest({{"host", "127.0.0.1:9999"}})).allowed);
    CHECK_FALSE(policy.check(makeRequest({{"host", "evil.example:7878"}})).allowed);
    CHECK_FALSE(policy.check(makeRequest({{"host", "127.0.0.1"}})).allowed);
    CHECK_FALSE(policy.check(makeRequest({})).allowed);
}

TEST_CASE("mcp security: origin allowlist")
{
    SecurityPolicy policy(7878, "");
    const std::pair<std::string, std::string> host{"host", "127.0.0.1:7878"};

    CHECK(policy.check(makeRequest({host, {"origin", "http://localhost:6274"}})).allowed);
    CHECK(policy.check(makeRequest({host, {"origin", "http://127.0.0.1"}})).allowed);
    CHECK(policy.check(makeRequest({host, {"origin", "https://[::1]:3000"}})).allowed);

    auto evil = policy.check(makeRequest({host, {"origin", "http://evil.example"}}));
    CHECK_FALSE(evil.allowed);
    CHECK(evil.status == 403);
    CHECK_FALSE(policy.check(makeRequest({host, {"origin", "null"}})).allowed);
    CHECK_FALSE(policy.check(makeRequest({host, {"origin", "http://localhost.evil.example"}})).allowed);
    CHECK_FALSE(policy.check(makeRequest({host, {"origin", "http://127.0.0.1.nip.io"}})).allowed);
    CHECK_FALSE(policy.check(makeRequest({host, {"origin", "http://localhost:80x"}})).allowed);
}

TEST_CASE("mcp security: bearer token")
{
    SecurityPolicy policy(7878, "s3cret");
    const std::pair<std::string, std::string> host{"host", "127.0.0.1:7878"};

    CHECK(policy.check(makeRequest({host, {"authorization", "Bearer s3cret"}})).allowed);
    CHECK(policy.check(makeRequest({host, {"authorization", "bearer s3cret"}})).allowed);

    auto missing = policy.check(makeRequest({host}));
    CHECK_FALSE(missing.allowed);
    CHECK(missing.status == 401);
    CHECK_FALSE(policy.check(makeRequest({host, {"authorization", "Bearer wrong"}})).allowed);
    CHECK_FALSE(policy.check(makeRequest({host, {"authorization", "Bearer s3cre"}})).allowed);
    CHECK_FALSE(policy.check(makeRequest({host, {"authorization", "Basic s3cret"}})).allowed);
}

TEST_CASE("mcp security: constant-time compare")
{
    CHECK(SecurityPolicy::constantTimeEquals("abc", "abc"));
    CHECK_FALSE(SecurityPolicy::constantTimeEquals("abc", "abd"));
    CHECK_FALSE(SecurityPolicy::constantTimeEquals("abc", "abcd"));
    CHECK(SecurityPolicy::constantTimeEquals("", ""));
}
