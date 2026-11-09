#include <doctest.h>

#include "protocol/JsonRpc.hpp"

using mcp::jsonrpc::MessageKind;
namespace errc = mcp::jsonrpc::errc;

TEST_CASE("mcp jsonrpc: request with integer and string ids round-trips")
{
    auto outcome = mcp::jsonrpc::parse(R"({"jsonrpc":"2.0","id":7,"method":"ping"})");
    REQUIRE(outcome.message);
    CHECK_FALSE(outcome.errorResponse);
    CHECK(outcome.message->kind == MessageKind::Request);
    CHECK(outcome.message->id == 7);
    CHECK(outcome.message->method == "ping");

    auto stringId = mcp::jsonrpc::parse(R"({"jsonrpc":"2.0","id":"abc","method":"tools/list","params":{}})");
    REQUIRE(stringId.message);
    CHECK(stringId.message->id == "abc");
    CHECK(stringId.message->params.is_object());

    nlohmann::json result = mcp::jsonrpc::makeResult(stringId.message->id, {{"ok", true}});
    CHECK(result["jsonrpc"] == "2.0");
    CHECK(result["id"] == "abc");
    CHECK(result["result"]["ok"] == true);
}

TEST_CASE("mcp jsonrpc: notification and client response kinds")
{
    auto note = mcp::jsonrpc::parse(R"({"jsonrpc":"2.0","method":"notifications/initialized"})");
    REQUIRE(note.message);
    CHECK(note.message->kind == MessageKind::Notification);
    CHECK(note.message->id.is_null());

    auto response = mcp::jsonrpc::parse(R"({"jsonrpc":"2.0","id":3,"result":{}})");
    REQUIRE(response.message);
    CHECK(response.message->kind == MessageKind::Response);
}

TEST_CASE("mcp jsonrpc: malformed input produces error responses")
{
    SUBCASE("parse error")
    {
        auto outcome = mcp::jsonrpc::parse("{not json");
        REQUIRE(outcome.errorResponse);
        CHECK((*outcome.errorResponse)["error"]["code"] == errc::parseError);
        CHECK((*outcome.errorResponse)["id"].is_null());
    }
    SUBCASE("batch rejected")
    {
        auto outcome = mcp::jsonrpc::parse(R"([{"jsonrpc":"2.0","id":1,"method":"ping"}])");
        REQUIRE(outcome.errorResponse);
        CHECK((*outcome.errorResponse)["error"]["code"] == errc::invalidRequest);
    }
    SUBCASE("wrong version keeps id")
    {
        auto outcome = mcp::jsonrpc::parse(R"({"jsonrpc":"1.0","id":5,"method":"ping"})");
        REQUIRE(outcome.errorResponse);
        CHECK((*outcome.errorResponse)["error"]["code"] == errc::invalidRequest);
        CHECK((*outcome.errorResponse)["id"] == 5);
    }
    SUBCASE("non-string method")
    {
        auto outcome = mcp::jsonrpc::parse(R"({"jsonrpc":"2.0","id":1,"method":42})");
        REQUIRE(outcome.errorResponse);
        CHECK((*outcome.errorResponse)["error"]["code"] == errc::invalidRequest);
    }
    SUBCASE("scalar params")
    {
        auto outcome = mcp::jsonrpc::parse(R"({"jsonrpc":"2.0","id":1,"method":"ping","params":3})");
        REQUIRE(outcome.errorResponse);
        CHECK((*outcome.errorResponse)["error"]["code"] == errc::invalidRequest);
    }
    SUBCASE("null id is invalid on a request")
    {
        auto outcome = mcp::jsonrpc::parse(R"({"jsonrpc":"2.0","id":null,"method":"ping"})");
        REQUIRE(outcome.errorResponse);
        CHECK((*outcome.errorResponse)["error"]["code"] == errc::invalidRequest);
    }
}

TEST_CASE("mcp jsonrpc: makeError attaches data only when given")
{
    nlohmann::json plain = mcp::jsonrpc::makeError(1, errc::methodNotFound, "nope");
    CHECK_FALSE(plain["error"].contains("data"));

    nlohmann::json withData = mcp::jsonrpc::makeError(1, errc::invalidParams, "bad", {{"field", "x"}});
    CHECK(withData["error"]["data"]["field"] == "x");
}
