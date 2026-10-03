#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/PromptRegistry.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"

#include <regex>
#include <set>
#include <string>

namespace
{
    struct PromptFixture
    {
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry tools;
        mcp::PromptRegistry prompts;

        PromptFixture()
        {
            mcp::tools::registerCoreTools(tools, queue);
            mcp::tools::registerCorePrompts(prompts);
        }

        std::string text(const char* name, const nlohmann::json& args)
        {
            const mcp::PromptDef* prompt = prompts.find(name);
            REQUIRE(prompt != nullptr);
            REQUIRE(prompt->build);
            const nlohmann::json messages = prompt->build(args);
            REQUIRE(messages.is_array());
            REQUIRE(messages.size() == 1);
            CHECK(messages[0]["role"] == "user");
            CHECK(messages[0]["content"]["type"] == "text");
            REQUIRE(messages[0]["content"]["text"].is_string());
            return messages[0]["content"]["text"].get<std::string>();
        }
    };

    // Every snake_case token: the prompts name tools that way and nothing else.
    std::set<std::string> snakeCaseTokens(const std::string& text)
    {
        static const std::regex pattern(R"(\b[a-z][a-z0-9]*(?:_[a-z0-9]+)+\b)");
        std::set<std::string> tokens;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), pattern); it != std::sregex_iterator(); ++it)
        {
            tokens.insert(it->str());
        }
        return tokens;
    }

    bool contains(const std::string& text, const std::string& needle)
    {
        return text.find(needle) != std::string::npos;
    }
}

TEST_SUITE("MCP prompts")
{
    TEST_CASE("both templates are registered with their optional arguments")
    {
        PromptFixture fixture;
        REQUIRE(fixture.prompts.all().size() == 2);

        const mcp::PromptDef* platformer = fixture.prompts.find("create_platformer_template");
        REQUIRE(platformer != nullptr);
        REQUIRE(platformer->arguments.size() == 3);
        CHECK(platformer->arguments[0].name == "name");
        CHECK(platformer->arguments[1].name == "playerSpeed");
        CHECK(platformer->arguments[2].name == "jumpHeight");
        for (const mcp::PromptArgument& argument : platformer->arguments)
        {
            CHECK_FALSE(argument.required);
        }

        const mcp::PromptDef* topDown = fixture.prompts.find("create_top_down_template");
        REQUIRE(topDown != nullptr);
        REQUIRE(topDown->arguments.size() == 2);
        CHECK(topDown->arguments[1].name == "cameraHeight");

        const nlohmann::json list = fixture.prompts.listJson();
        REQUIRE(list.size() == 2);
        CHECK(list[0]["name"] == "create_platformer_template");
    }

    TEST_CASE("every tool named by a prompt exists in the core tool registry")
    {
        PromptFixture fixture;
        REQUIRE(fixture.tools.size() > 0);

        for (const char* name : {"create_platformer_template", "create_top_down_template"})
        {
            CAPTURE(name);
            const std::string text = fixture.text(name, nlohmann::json::object());
            const std::set<std::string> tokens = snakeCaseTokens(text);
            REQUIRE_FALSE(tokens.empty());
            for (const std::string& token : tokens)
            {
                CAPTURE(token);
                CHECK(fixture.tools.find(token) != nullptr);
            }

            // The walkthrough the plan requires, in tool terms.
            for (const char* tool : {"scene_new", "entity_create", "component_add", "material_create",
                                     "material_assign", "script_write", "scripts_build", "script_attach",
                                     "play_start", "logs_read", "play_stop", "viewport_screenshot", "scene_save"})
            {
                CAPTURE(tool);
                CHECK(tokens.count(tool) == 1);
            }
            CHECK(contains(text, "vf://docs/mtype-api"));
            CHECK_FALSE(contains(text, "camera_"));
            // Component types are the component_add enum values.
            for (const char* type : {"'DirectionalLight'", "'Camera'", "'Mesh'", "'RigidBody'", "'Collider'"})
            {
                CAPTURE(type);
                CHECK(contains(text, type));
            }
        }
    }

    TEST_CASE("defaults and argument substitution")
    {
        PromptFixture fixture;

        const std::string defaults = fixture.text("create_platformer_template", nlohmann::json::object());
        CHECK(contains(defaults, "game/Platformer/PlatformerPlayerController.mt"));
        CHECK(contains(defaults, "+/-6.0"));
        CHECK(contains(defaults, "sqrt(2.0 * 9.81 * 2.0)"));
        CHECK(contains(defaults, "scenes/Platformer.vfScene"));

        // MCP prompt arguments are strings; empty means "not given".
        const std::string custom = fixture.text("create_platformer_template",
            {{"name", "my cool game"}, {"playerSpeed", " 7.5 "}, {"jumpHeight", ""}});
        CHECK(contains(custom, "game/MyCoolGame/MyCoolGamePlayerController.mt"));
        CHECK(contains(custom, "+/-7.5"));
        CHECK(contains(custom, "sqrt(2.0 * 9.81 * 2.0)"));

        const std::string topDown = fixture.text("create_top_down_template",
            {{"name", "3d"}, {"cameraHeight", 20}});
        CHECK(contains(topDown, "Game3dPlayerMover"));
        CHECK(contains(topDown, "[0, 20.0, 0]"));

        const std::string unnamed = fixture.text("create_top_down_template", {{"name", "!!!"}});
        CHECK(contains(unnamed, "TopDownCameraFollow"));
        CHECK(contains(unnamed, "[0, 15.0, 0]"));
    }

    TEST_CASE("bad numeric arguments throw ArgError")
    {
        PromptFixture fixture;
        const mcp::PromptDef* platformer = fixture.prompts.find("create_platformer_template");
        const mcp::PromptDef* topDown = fixture.prompts.find("create_top_down_template");
        REQUIRE(platformer != nullptr);
        REQUIRE(topDown != nullptr);

        CHECK_THROWS_AS(platformer->build({{"playerSpeed", "fast"}}), mcp::ArgError);
        CHECK_THROWS_AS(platformer->build({{"playerSpeed", "5m"}}), mcp::ArgError);
        CHECK_THROWS_AS(platformer->build({{"playerSpeed", "0"}}), mcp::ArgError);
        CHECK_THROWS_AS(platformer->build({{"jumpHeight", "nan"}}), mcp::ArgError);
        CHECK_THROWS_AS(platformer->build({{"jumpHeight", "1e400"}}), mcp::ArgError);
        CHECK_THROWS_AS(platformer->build({{"jumpHeight", true}}), mcp::ArgError);
        CHECK_THROWS_AS(topDown->build({{"cameraHeight", "-3"}}), mcp::ArgError);
        CHECK_THROWS_AS(topDown->build({{"cameraHeight", 1000}}), mcp::ArgError);
        CHECK_THROWS_AS(topDown->build({{"name", 5}}), mcp::ArgError);
    }
}
