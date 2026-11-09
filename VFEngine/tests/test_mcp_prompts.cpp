#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/PromptRegistry.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"

#include <regex>
#include <set>
#include <string>
#include <utility>

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
    TEST_CASE("the four templates are registered with their optional arguments")
    {
        PromptFixture fixture;
        REQUIRE(fixture.prompts.all().size() == 4);

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

        // VK-1659
        const mcp::PromptDef* hud = fixture.prompts.find("create_hud");
        REQUIRE(hud != nullptr);
        REQUIRE(hud->arguments.size() == 1);
        CHECK(hud->arguments[0].name == "name");
        CHECK_FALSE(hud->arguments[0].required);

        // VK-1660
        const mcp::PromptDef* enemy = fixture.prompts.find("create_enemy_ai");
        REQUIRE(enemy != nullptr);
        REQUIRE(enemy->arguments.size() == 3);
        CHECK(enemy->arguments[0].name == "name");
        CHECK(enemy->arguments[1].name == "chaseRange");
        CHECK(enemy->arguments[2].name == "attackRange");
        for (const mcp::PromptArgument& argument : enemy->arguments)
        {
            CHECK_FALSE(argument.required);
        }

        const nlohmann::json list = fixture.prompts.listJson();
        REQUIRE(list.size() == 4);
        CHECK(list[0]["name"] == "create_platformer_template");
        CHECK(list[3]["name"] == "create_enemy_ai");
    }

    TEST_CASE("create_hud walks the HUD acceptance with registered tools only (VK-1659)")
    {
        PromptFixture fixture;

        const std::string text = fixture.text("create_hud", nlohmann::json::object());
        const std::set<std::string> tokens = snakeCaseTokens(text);
        REQUIRE_FALSE(tokens.empty());
        for (const std::string& token : tokens)
        {
            CAPTURE(token);
            CHECK(fixture.tools.find(token) != nullptr);
        }
        for (const char* tool : {"editor_get_info", "ui_list_widget_types", "ui_create_canvas", "ui_add_widget",
                                 "ui_get", "ui_set", "ui_click", "script_write", "scripts_build", "script_attach",
                                 "scene_save", "play_start", "viewport_screenshot", "logs_read", "play_stop"})
        {
            CAPTURE(tool);
            CHECK(tokens.count(tool) == 1);
        }
        CHECK(fixture.tools.find("undo") != nullptr);
        CHECK(contains(text, "then undo"));
        CHECK(contains(text, "section 11 (UI) of vf://docs/mtype-api"));
        CHECK(contains(text, "game/Game/GameHudController.mt"));
        CHECK(contains(text, "\"placement\": {\"preset\": \"topLeft\", \"size\": [320, 28], \"margin\": 24}"));
        CHECK(contains(text, "HealthBar (24, 24, 320, 28), ScoreLabel (1596, 20, 300, 48) and PauseButton "
                             "(880, 20, 160, 56)"));

        const std::string named = fixture.text("create_hud", {{"name", "space run"}});
        CHECK(contains(named, "game/SpaceRun/SpaceRunHudController.mt"));
        CHECK(contains(named, "renamed to SpaceRunHudController"));
        CHECK_THROWS_AS(fixture.prompts.find("create_hud")->build({{"name", 5}}), mcp::ArgError);
    }

    TEST_CASE("create_enemy_ai walks the AI acceptance with registered tools only (VK-1660)")
    {
        PromptFixture fixture;

        const std::string text = fixture.text("create_enemy_ai", nlohmann::json::object());
        const std::set<std::string> tokens = snakeCaseTokens(text);
        REQUIRE_FALSE(tokens.empty());
        for (const std::string& token : tokens)
        {
            CAPTURE(token);
            CHECK(fixture.tools.find(token).get() != nullptr);
        }
        for (const char* tool : {"editor_get_info", "navmesh_get_info", "scene_get_hierarchy", "navmesh_bake",
                                 "entity_create", "component_add", "navmesh_find_path", "route_create", "route_get",
                                 "script_write", "scripts_build", "bt_list_node_types", "bt_create", "bt_assign",
                                 "bt_get_blackboard", "scene_save", "play_start", "bt_debug_state", "logs_read",
                                 "play_stop", "bt_edit", "bt_get", "scene_load", "viewport_screenshot"})
        {
            CAPTURE(tool);
            CHECK(tokens.count(tool) == 1);
        }
        CHECK(contains(text, "vf://docs/bt"));
        CHECK(contains(text, "section 12 of vf://docs/mtype-api with the class renamed to EnemyMeleeAttack"));
        CHECK(contains(text, "script_write 'game/ai/EnemyMeleeAttack.mt'"));
        CHECK(contains(text, "'NavmeshAgent' with data {\"radius\": 0.4, \"baseOffset\": 1}"));
        CHECK(contains(text, "\"params\": {\"compareValue\": 15.0}"));

        // The tree handed to bt_create is the one line after "<this tree>}:", and it is valid JSON.
        const std::string marker = "\"tree\": <this tree>}:\n";
        const std::size_t begin = text.find(marker);
        REQUIRE(begin != std::string::npos);
        const std::size_t treeBegin = begin + marker.size();
        const std::size_t treeEnd = text.find('\n', treeBegin);
        REQUIRE(treeEnd != std::string::npos);
        const nlohmann::json tree = nlohmann::json::parse(text.substr(treeBegin, treeEnd - treeBegin));
        CHECK(tree["name"] == "EnemyAI");
        REQUIRE(tree["blackboard"].size() == 4);
        CHECK(tree["blackboard"][0]["name"] == "target");
        CHECK(tree["blackboard"][0]["type"] == "Entity");
        CHECK(tree["root"]["type"] == "Service");
        CHECK(tree["root"]["params"]["serviceType"] == "TargetDistance");
        const nlohmann::json& branches = tree["root"]["child"]["children"];
        REQUIRE(branches.size() == 3);
        CHECK(branches[0]["params"]["compareValue"] == 2.0);
        CHECK(branches[0]["child"]["type"] == "ScriptTask");
        CHECK(branches[0]["child"]["params"]["script"] == "game/ai/EnemyMeleeAttack.mt");
        CHECK(branches[1]["params"]["compareValue"] == 10.0);
        CHECK(branches[1]["child"]["type"] == "MoveTo");
        CHECK(branches[1]["child"]["params"]["arrivalDistance"] == 1.5);
        CHECK(branches[2]["type"] == "FollowRoute");
        CHECK(branches[2]["params"]["routeKey"] == "route");

        const std::string named = fixture.text("create_enemy_ai",
            {{"name", "orc grunt"}, {"chaseRange", "12"}, {"attackRange", " 3 "}});
        CHECK(contains(named, "game/ai/OrcGruntMeleeAttack.mt"));
        CHECK(contains(named, "renamed to OrcGruntMeleeAttack"));
        CHECK(contains(named, "ai/OrcGruntAI.vfBehaviorTree"));
        CHECK(contains(named, "\"compareValue\": 3.0"));
        CHECK(contains(named, "\"compareValue\": 12.0"));
        CHECK(contains(named, "\"arrivalDistance\": 2.25"));

        const mcp::PromptDef* prompt = fixture.prompts.find("create_enemy_ai");
        REQUIRE(prompt != nullptr);
        CHECK_THROWS_AS(prompt->build({{"name", 5}}), mcp::ArgError);
        CHECK_THROWS_AS(prompt->build({{"chaseRange", "1"}}), mcp::ArgError);
        CHECK_THROWS_AS(prompt->build({{"attackRange", "25"}}), mcp::ArgError);
        CHECK_THROWS_AS(prompt->build({{"chaseRange", "5"}, {"attackRange", "5"}}), mcp::ArgError);
        CHECK_NOTHROW(prompt->build({{"chaseRange", "5"}, {"attackRange", "4.5"}}));
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

    TEST_CASE("both templates offer the optional animated player (VK-1656)")
    {
        PromptFixture fixture;

        const std::pair<const char*, const char*> templates[] = {
            {"create_platformer_template", "PlatformerPlayerController"},
            {"create_top_down_template", "TopDownPlayerMover"}
        };
        for (const auto& entry : templates)
        {
            const char* name = entry.first;
            const std::string script = entry.second;
            CAPTURE(name);
            const std::string text = fixture.text(name, nlohmann::json::object());
            const std::set<std::string> tokens = snakeCaseTokens(text);
            for (const char* tool : {"assets_import", "animation_list_clips", "animation_set_state_machine",
                                     "animation_preview", "animation_get_runtime_state"})
            {
                CAPTURE(tool);
                CHECK(tokens.count(tool) == 1);
            }
            CHECK(contains(text, "13. Optional animated player"));
            CHECK(contains(text, "'Animator' with data {\"defaultClip\""));
            CHECK(contains(text, "In " + script + " set 'speed' every frame"));
            CHECK(contains(text, "vf://docs/mtype-api/engine/Animator"));
            CHECK(contains(text, "14. Build, attach, save and play-test:"));
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
