#include <doctest.h>

#include "protocol/ToolRegistry.hpp"

TEST_CASE("mcp tool result: image block follows the text block")
{
    const mcp::ToolResult result = mcp::ToolResult::image("QUJD", "image/png", {{"width", 2}, {"height", 1}}, "shot");
    const nlohmann::json json = result.toJson();

    CHECK(json["isError"] == false);
    REQUIRE(json["content"].is_array());
    REQUIRE(json["content"].size() == 2);

    const nlohmann::json& text = json["content"][0];
    CHECK(text["type"] == "text");
    CHECK(text["text"] == "shot");

    const nlohmann::json& image = json["content"][1];
    CHECK(image["type"] == "image");
    CHECK(image["data"] == "QUJD");
    CHECK(image["mimeType"] == "image/png");
    CHECK(image.size() == 3);

    REQUIRE(json.contains("structuredContent"));
    CHECK(json["structuredContent"]["width"] == 2);
    CHECK(json["structuredContent"]["height"] == 1);
}

TEST_CASE("mcp tool result: image without text falls back to the structured dump")
{
    const nlohmann::json json = mcp::ToolResult::image("AA==", "image/png", {{"width", 1}}).toJson();
    REQUIRE(json["content"].size() == 2);
    CHECK(json["content"][0]["type"] == "text");
    CHECK(nlohmann::json::parse(json["content"][0]["text"].get<std::string>()) == nlohmann::json{{"width", 1}});
    CHECK(json["content"][1]["type"] == "image");
}

TEST_CASE("mcp tool result: ok and error results are unchanged")
{
    const nlohmann::json error = mcp::ToolResult::error("bad thing").toJson();
    CHECK(error["isError"] == true);
    REQUIRE(error["content"].size() == 1);
    CHECK(error["content"][0]["type"] == "text");
    CHECK(error["content"][0]["text"] == "bad thing");
    CHECK_FALSE(error.contains("structuredContent"));

    const nlohmann::json ok = mcp::ToolResult::ok({{"value", 3}}).toJson();
    CHECK(ok["isError"] == false);
    REQUIRE(ok["content"].size() == 1);
    CHECK(ok["content"][0]["type"] == "text");
    CHECK(ok["structuredContent"]["value"] == 3);

    const nlohmann::json scalar = mcp::ToolResult::ok(5).toJson();
    CHECK(scalar["structuredContent"]["result"] == 5);
}
