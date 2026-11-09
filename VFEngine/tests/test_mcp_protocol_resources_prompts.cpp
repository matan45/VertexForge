#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/McpServer.hpp"
#include "protocol/PromptRegistry.hpp"
#include "protocol/ResourceRegistry.hpp"
#include "protocol/ToolRegistry.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    struct ProtocolFixture
    {
        mcp::ToolRegistry tools;
        mcp::ResourceRegistry resources;
        mcp::PromptRegistry prompts;
        std::vector<std::string> templateSuffixes;
        int contributorCalls = 0;

        ProtocolFixture()
        {
            mcp::ResourceDef hierarchy;
            hierarchy.uri = "vf://scene/hierarchy";
            hierarchy.name = "scene-hierarchy";
            hierarchy.title = "Scene hierarchy";
            hierarchy.description = "Entity tree";
            hierarchy.mimeType = "application/json";
            hierarchy.reader = []()
            {
                return mcp::ResourceContents{"", "", R"({"root":1})"};
            };
            resources.add(hierarchy);

            mcp::ResourceDef missing;
            missing.uri = "vf://missing";
            missing.name = "missing";
            missing.mimeType = "text/plain";
            missing.affinity = mcp::ThreadAffinity::Worker;
            missing.reader = []() -> mcp::ResourceContents
            {
                throw mcp::ResourceNotFound("gone");
            };
            resources.add(missing);

            mcp::ResourceDef broken;
            broken.uri = "vf://broken";
            broken.name = "broken";
            broken.mimeType = "text/plain";
            broken.reader = []() -> mcp::ResourceContents
            {
                throw std::runtime_error("disk on fire");
            };
            resources.add(broken);

            mcp::ResourceTemplateDef scripts;
            scripts.uriTemplate = "vf://scripts/{+path}";
            scripts.prefix = "vf://scripts/";
            scripts.name = "script";
            scripts.description = "A project script";
            scripts.mimeType = "text/x-mtype";
            scripts.affinity = mcp::ThreadAffinity::Worker;
            scripts.reader = [this](const std::string& uri, const std::string& suffix)
            {
                templateSuffixes.push_back(suffix);
                if (suffix == "nope.mt")
                {
                    throw mcp::ResourceNotFound("no such script");
                }
                return mcp::ResourceContents{uri, "", "// " + suffix};
            };
            resources.addTemplate(scripts);

            // Longer prefix wins over vf://scripts/.
            mcp::ResourceTemplateDef libScripts = scripts;
            libScripts.uriTemplate = "vf://scripts/lib/{+path}";
            libScripts.prefix = "vf://scripts/lib/";
            libScripts.name = "lib-script";
            libScripts.reader = [](const std::string& uri, const std::string& suffix)
            {
                return mcp::ResourceContents{uri, "text/plain", "lib:" + suffix};
            };
            resources.addTemplate(libScripts);

            mcp::ResourceListContributor gameScripts;
            gameScripts.list = [this]()
            {
                ++contributorCalls;
                mcp::ResourceDef script;
                script.uri = "vf://scripts/game/Player.mt";
                script.name = "game/Player.mt";
                script.mimeType = "text/x-mtype";
                // Duplicate of a static resource: listed once.
                mcp::ResourceDef duplicate;
                duplicate.uri = "vf://scene/hierarchy";
                duplicate.name = "dup";
                return std::vector<mcp::ResourceDef>{script, duplicate};
            };
            resources.addContributor(gameScripts);

            mcp::ResourceListContributor failing;
            failing.affinity = mcp::ThreadAffinity::Worker;
            failing.list = []() -> std::vector<mcp::ResourceDef>
            {
                throw std::runtime_error("No handler registered");
            };
            resources.addContributor(failing);

            mcp::PromptDef platformer;
            platformer.name = "create_platformer_template";
            platformer.title = "Platformer";
            platformer.description = "Build a platformer";
            platformer.arguments = {{"name", "Game name", true}, {"playerSpeed", "Speed", false}};
            platformer.build = [](const nlohmann::json& args)
            {
                mcp::ArgReader reader(args);
                const std::string speed = reader.optString("playerSpeed", "5");
                if (speed == "fast")
                {
                    throw mcp::ArgError("playerSpeed must be a number");
                }
                return nlohmann::json::array({nlohmann::json{
                    {"role", "user"},
                    {"content", {{"type", "text"}, {"text", "Make " + reader.requireString("name") + " at " + speed}}}
                }});
            };
            prompts.add(platformer);
        }
    };

    nlohmann::json call(mcp::McpServer& server, const std::string& method, nlohmann::json params = nullptr)
    {
        nlohmann::json request{{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}};
        if (!params.is_null())
        {
            request["params"] = std::move(params);
        }
        auto response = server.handleBody(request.dump());
        REQUIRE(response);
        return *response;
    }
}

TEST_SUITE("McpProtocolResourcesPrompts")
{
    TEST_CASE("resources/list merges static resources and contributors")
    {
        ProtocolFixture fixture;
        mcp::McpServer server(fixture.tools, fixture.resources, fixture.prompts, {});

        int invoked = 0;
        server.setMainThreadInvoker([&](std::function<nlohmann::json()> task, std::chrono::milliseconds)
        {
            ++invoked;
            return task();
        });

        nlohmann::json r = call(server, "resources/list", {{"cursor", "ignored"}});
        const nlohmann::json& list = r["result"]["resources"];
        REQUIRE(list.size() == 4);
        CHECK(list[0]["uri"] == "vf://scene/hierarchy");
        CHECK(list[0]["title"] == "Scene hierarchy");
        CHECK(list[0]["mimeType"] == "application/json");
        CHECK_FALSE(list[1].contains("title"));
        CHECK(list[3]["uri"] == "vf://scripts/game/Player.mt");
        CHECK(fixture.contributorCalls == 1);
        // Main-affinity contributor went through the invoker; the failing Worker
        // contributor was skipped without failing the request.
        CHECK(invoked == 1);
    }

    TEST_CASE("resources/templates/list describes the templates")
    {
        ProtocolFixture fixture;
        mcp::McpServer server(fixture.tools, fixture.resources, fixture.prompts, {});

        nlohmann::json r = call(server, "resources/templates/list");
        const nlohmann::json& list = r["result"]["resourceTemplates"];
        REQUIRE(list.size() == 2);
        CHECK(list[0]["uriTemplate"] == "vf://scripts/{+path}");
        CHECK(list[0]["name"] == "script");
        CHECK(list[0]["mimeType"] == "text/x-mtype");
        CHECK_FALSE(list[0].contains("prefix"));
    }

    TEST_CASE("resources/read")
    {
        ProtocolFixture fixture;
        mcp::McpServer server(fixture.tools, fixture.resources, fixture.prompts, {});

        SUBCASE("exact URI fills uri and mimeType defaults")
        {
            nlohmann::json r = call(server, "resources/read", {{"uri", "vf://scene/hierarchy"}});
            const nlohmann::json& contents = r["result"]["contents"];
            REQUIRE(contents.size() == 1);
            CHECK(contents[0]["uri"] == "vf://scene/hierarchy");
            CHECK(contents[0]["mimeType"] == "application/json");
            CHECK(contents[0]["text"] == R"({"root":1})");
        }
        SUBCASE("template URI is percent-decoded")
        {
            nlohmann::json r = call(server, "resources/read", {{"uri", "vf://scripts/game%2Fsub%2FA+B.mt"}});
            REQUIRE(fixture.templateSuffixes.size() == 1);
            CHECK(fixture.templateSuffixes[0] == "game/sub/A+B.mt");
            CHECK(r["result"]["contents"][0]["text"] == "// game/sub/A+B.mt");
            CHECK(r["result"]["contents"][0]["mimeType"] == "text/x-mtype");
        }
        SUBCASE("longest template prefix wins")
        {
            nlohmann::json r = call(server, "resources/read", {{"uri", "vf://scripts/lib/math/Vec3f.mt"}});
            CHECK(r["result"]["contents"][0]["text"] == "lib:math/Vec3f.mt");
            CHECK(r["result"]["contents"][0]["mimeType"] == "text/plain");
        }
        SUBCASE("malformed percent-encoding is invalid params")
        {
            nlohmann::json r = call(server, "resources/read", {{"uri", "vf://scripts/a%2"}});
            CHECK(r["error"]["code"] == mcp::jsonrpc::errc::invalidParams);
            CHECK(fixture.templateSuffixes.empty());
        }
        SUBCASE("unknown URI is -32002 with the uri in data")
        {
            nlohmann::json r = call(server, "resources/read", {{"uri", "vf://nothing"}});
            CHECK(r["error"]["code"] == mcp::jsonrpc::errc::resourceNotFound);
            CHECK(r["error"]["data"]["uri"] == "vf://nothing");
        }
        SUBCASE("ResourceNotFound from a reader is -32002")
        {
            CHECK(call(server, "resources/read", {{"uri", "vf://missing"}})["error"]["code"] ==
                  mcp::jsonrpc::errc::resourceNotFound);
            CHECK(call(server, "resources/read", {{"uri", "vf://scripts/nope.mt"}})["error"]["code"] ==
                  mcp::jsonrpc::errc::resourceNotFound);
        }
        SUBCASE("reader exception is -32603")
        {
            nlohmann::json r = call(server, "resources/read", {{"uri", "vf://broken"}});
            CHECK(r["error"]["code"] == mcp::jsonrpc::errc::internalError);
            CHECK(r["error"]["message"] == "disk on fire");
        }
        SUBCASE("missing or non-string uri is invalid params")
        {
            CHECK(call(server, "resources/read", nlohmann::json::object())["error"]["code"] ==
                  mcp::jsonrpc::errc::invalidParams);
            CHECK(call(server, "resources/read", {{"uri", 5}})["error"]["code"] == mcp::jsonrpc::errc::invalidParams);
        }
        SUBCASE("main-thread timeout is -32001")
        {
            server.setMainThreadInvoker([](std::function<nlohmann::json()>, std::chrono::milliseconds) -> nlohmann::json
            {
                throw mcp::MainThreadTimeout("busy");
            });
            nlohmann::json r = call(server, "resources/read", {{"uri", "vf://scene/hierarchy"}});
            CHECK(r["error"]["code"] == mcp::jsonrpc::errc::mainThreadTimeout);
        }
    }

    TEST_CASE("percentDecode")
    {
        CHECK(mcp::ResourceRegistry::percentDecode("a%2Fb%2fc") == std::optional<std::string>("a/b/c"));
        CHECK(mcp::ResourceRegistry::percentDecode("a+b") == std::optional<std::string>("a+b"));
        CHECK(mcp::ResourceRegistry::percentDecode("%20") == std::optional<std::string>(" "));
        CHECK_FALSE(mcp::ResourceRegistry::percentDecode("%"));
        CHECK_FALSE(mcp::ResourceRegistry::percentDecode("abc%4"));
        CHECK_FALSE(mcp::ResourceRegistry::percentDecode("%zz"));
        CHECK_FALSE(mcp::ResourceRegistry::percentDecode("a%00b"));
    }

    TEST_CASE("prompts/list and prompts/get")
    {
        ProtocolFixture fixture;
        mcp::McpServer server(fixture.tools, fixture.resources, fixture.prompts, {});

        SUBCASE("list describes the arguments")
        {
            nlohmann::json r = call(server, "prompts/list");
            const nlohmann::json& list = r["result"]["prompts"];
            REQUIRE(list.size() == 1);
            CHECK(list[0]["name"] == "create_platformer_template");
            CHECK(list[0]["title"] == "Platformer");
            REQUIRE(list[0]["arguments"].size() == 2);
            CHECK(list[0]["arguments"][0]["name"] == "name");
            CHECK(list[0]["arguments"][0]["required"] == true);
            CHECK(list[0]["arguments"][1]["required"] == false);
        }
        SUBCASE("get builds the messages")
        {
            nlohmann::json r = call(server, "prompts/get",
                                    {{"name", "create_platformer_template"}, {"arguments", {{"name", "Jumpy"}}}});
            CHECK(r["result"]["description"] == "Build a platformer");
            REQUIRE(r["result"]["messages"].size() == 1);
            CHECK(r["result"]["messages"][0]["content"]["text"] == "Make Jumpy at 5");
        }
        SUBCASE("unknown prompt is invalid params")
        {
            CHECK(call(server, "prompts/get", {{"name", "nope"}})["error"]["code"] == mcp::jsonrpc::errc::invalidParams);
        }
        SUBCASE("missing required argument is invalid params")
        {
            nlohmann::json r = call(server, "prompts/get", {{"name", "create_platformer_template"}});
            CHECK(r["error"]["code"] == mcp::jsonrpc::errc::invalidParams);
            CHECK(r["error"]["message"].get<std::string>().find("name") != std::string::npos);
        }
        SUBCASE("non-string argument is invalid params")
        {
            nlohmann::json r = call(server, "prompts/get",
                                    {{"name", "create_platformer_template"},
                                     {"arguments", {{"name", "Jumpy"}, {"playerSpeed", 7}}}});
            CHECK(r["error"]["code"] == mcp::jsonrpc::errc::invalidParams);
        }
        SUBCASE("ArgError from the builder is invalid params")
        {
            nlohmann::json r = call(server, "prompts/get",
                                    {{"name", "create_platformer_template"},
                                     {"arguments", {{"name", "Jumpy"}, {"playerSpeed", "fast"}}}});
            CHECK(r["error"]["code"] == mcp::jsonrpc::errc::invalidParams);
        }
    }
}
