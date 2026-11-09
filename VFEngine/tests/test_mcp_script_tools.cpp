#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ToolRegistry.hpp"
#include "tools/CoreTools.hpp"
#include "tools/PathSandbox.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/EditorModeEvents.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scripting/ScriptingEvents.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    struct DispatcherScope
    {
        DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
        }

        ~DispatcherScope()
        {
            events::EventDispatcher::instance().clear();
        }
    };

    fs::path makeTestDirectory(const char* name)
    {
        auto directory = fs::temp_directory_path() / name;
        fs::remove_all(directory);
        fs::create_directories(directory);
        return fs::weakly_canonical(directory);
    }

    std::string readFile(const fs::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    }

    struct ScriptToolsFixture
    {
        mcp::MainThreadQueue queue;
        mcp::ToolRegistry registry;

        ScriptToolsFixture()
        {
            mcp::tools::registerScriptTools(registry, mcp::tools::ToolContext{queue});
        }

        const mcp::ToolDef& tool(const char* name) const
        {
            auto found = registry.find(name);
            REQUIRE(found != nullptr);
            return *found;
        }

        // Worker-affinity tools block on runOnMain; play the editor main thread here.
        mcp::ToolResult callWorker(const char* name, const nlohmann::json& args)
        {
            const mcp::ToolDef& def = tool(name);
            REQUIRE(def.affinity == mcp::ThreadAffinity::Worker);
            auto future = std::async(std::launch::async, [&def, args]() { return def.handler(args); });
            while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
            {
                queue.drain(std::chrono::milliseconds(8));
            }
            return future.get();
        }
    };

    void registerPlayMode(bool playing)
    {
        events::EventDispatcher::instance().registerQueryHandler<events::editor::IsPlayModeQuery>(
            [playing](const events::editor::IsPlayModeQuery&) { return playing; });
    }
}

TEST_SUITE("MCP script tools")
{
    TEST_CASE("PathSandbox accepts nested paths inside the root")
    {
        const fs::path root = makeTestDirectory("VertexForge_McpSandbox_Accept");
        std::string error;

        auto nested = mcp::tools::resolveInside(root, "game/player/Mover.mt", error, ".mt");
        REQUIRE(nested.has_value());
        CHECK(*nested == root / "game" / "player" / "Mover.mt");

        // A ".." that stays inside the root is fine.
        auto dotted = mcp::tools::resolveInside(root, "game/sub/../Foo.mt", error, ".mt");
        REQUIRE(dotted.has_value());
        CHECK(*dotted == root / "game" / "Foo.mt");

        // Absolute paths are accepted when they resolve inside the root.
        auto absolute = mcp::tools::resolveInside(root, mcp::tools::pathToUtf8(root / "game" / "A.mt"), error, ".mt");
        REQUIRE(absolute.has_value());
        CHECK(*absolute == root / "game" / "A.mt");

        // Without an extension requirement the root itself is valid (folder targets).
        auto self = mcp::tools::resolveInside(root, ".", error);
        REQUIRE(self.has_value());
        CHECK(*self == root);

        fs::remove_all(root);
    }

    TEST_CASE("PathSandbox rejects escapes, foreign absolute paths and wrong extensions")
    {
        const fs::path root = makeTestDirectory("VertexForge_McpSandbox_Reject") / "scripts";
        fs::create_directories(root);
        std::string error;

        CHECK_FALSE(mcp::tools::resolveInside(root, "../outside.mt", error, ".mt").has_value());
        CHECK_FALSE(error.empty());

        error.clear();
        CHECK_FALSE(mcp::tools::resolveInside(root, "game/../../outside.mt", error, ".mt").has_value());
        CHECK_FALSE(error.empty());

        // Sibling folder sharing the root's name as a prefix.
        const fs::path sibling = root.parent_path() / "scripts2" / "x.mt";
        CHECK_FALSE(mcp::tools::resolveInside(root, mcp::tools::pathToUtf8(sibling), error, ".mt").has_value());

        const fs::path foreign = fs::temp_directory_path() / "VertexForge_McpSandbox_Foreign" / "x.mt";
        CHECK_FALSE(mcp::tools::resolveInside(root, mcp::tools::pathToUtf8(foreign), error, ".mt").has_value());

        CHECK_FALSE(mcp::tools::resolveInside(root, "game/Foo.txt", error, ".mt").has_value());
        CHECK_FALSE(mcp::tools::resolveInside(root, "game/Foo", error, ".mt").has_value());
        CHECK_FALSE(mcp::tools::resolveInside(root, "game/.mt", error, ".mt").has_value());
        CHECK_FALSE(mcp::tools::resolveInside(root, "", error, ".mt").has_value());

        fs::remove_all(root.parent_path());
    }

    TEST_CASE("scripts_build maps the build result, including compile errors")
    {
        DispatcherScope dispatcherScope;
        ScriptToolsFixture fixture;
        registerPlayMode(false);

        int builds = 0;
        events::EventDispatcher::instance().registerCommandHandler<events::scripting::BuildScriptsCommand>(
            [&builds](const events::scripting::BuildScriptsCommand&)
            {
                ++builds;
                services::ScriptBuildResult result;
                result.success = false;
                result.filesCompiled = 3;
                result.filesFailed = 1;
                result.errors = {"game/Player.mt: Expected ';' at line 12", "game/Enemy.mt: Unknown type 'Vec'"};
                return result;
            });

        const mcp::ToolDef& build = fixture.tool("scripts_build");
        CHECK(build.affinity == mcp::ThreadAffinity::Main);

        mcp::ToolResult result = build.handler(nlohmann::json::object());
        CHECK(builds == 1);
        CHECK_FALSE(result.isError);
        CHECK(result.structured["success"] == false);
        CHECK(result.structured["filesCompiled"] == 3);
        CHECK(result.structured["filesFailed"] == 1);
        REQUIRE(result.structured["errors"].size() == 2);
        CHECK(result.structured["errors"][0] == "game/Player.mt: Expected ';' at line 12");
        CHECK(result.structured["errors"][1] == "game/Enemy.mt: Unknown type 'Vec'");
        CHECK(result.text.find("Build FAILED") != std::string::npos);
        CHECK(result.text.find("game/Enemy.mt: Unknown type 'Vec'") != std::string::npos);
    }

    TEST_CASE("scripts_build reports success and refuses to run in Play mode")
    {
        DispatcherScope dispatcherScope;
        ScriptToolsFixture fixture;

        int builds = 0;
        events::EventDispatcher::instance().registerCommandHandler<events::scripting::BuildScriptsCommand>(
            [&builds](const events::scripting::BuildScriptsCommand&)
            {
                ++builds;
                services::ScriptBuildResult result;
                result.filesCompiled = 5;
                return result;
            });

        SUBCASE("edit mode builds")
        {
            registerPlayMode(false);
            mcp::ToolResult result = fixture.tool("scripts_build").handler(nlohmann::json::object());
            CHECK(builds == 1);
            CHECK_FALSE(result.isError);
            CHECK(result.structured["success"] == true);
            CHECK(result.structured["filesCompiled"] == 5);
            CHECK(result.structured["errors"].empty());
        }

        SUBCASE("play mode is refused without building")
        {
            registerPlayMode(true);
            mcp::ToolResult result = fixture.tool("scripts_build").handler(nlohmann::json::object());
            CHECK(builds == 0);
            CHECK(result.isError);
        }
    }

    TEST_CASE("script_write writes inside game/ and rejects paths outside it")
    {
        DispatcherScope dispatcherScope;
        ScriptToolsFixture fixture;
        const fs::path assets = makeTestDirectory("VertexForge_McpScriptWrite");

        events::EventDispatcher::instance().registerQueryHandler<events::project::GetCurrentProjectQuery>(
            [assets](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
            {
                config::ProjectConfig config;
                config.projectName = "McpScriptWrite";
                config.workingDirectory = assets.string();
                return config;
            });

        const std::string source = "@Script\npublic class Mover {\n}\n";
        mcp::ToolResult written = fixture.callWorker("script_write", {{"path", "game/player/Mover.mt"}, {"source", source}});
        CHECK_FALSE(written.isError);
        CHECK(written.structured["path"] == "game/player/Mover.mt");
        CHECK(written.structured["created"] == true);
        CHECK(readFile(assets / "scripts" / "game" / "player" / "Mover.mt") == source);

        mcp::ToolResult read = fixture.callWorker("script_read", {{"path", "game/player/Mover.mt"}});
        CHECK_FALSE(read.isError);
        CHECK(read.structured["source"] == source);

        CHECK_THROWS_AS(fixture.callWorker("script_write", {{"path", "lib/engine/Evil.mt"}, {"source", source}}),
                        mcp::ArgError);
        CHECK_THROWS_AS(fixture.callWorker("script_write", {{"path", "game/../../Evil.mt"}, {"source", source}}),
                        mcp::ArgError);
        CHECK_THROWS_AS(fixture.callWorker("script_write", {{"path", "game/Evil.txt"}, {"source", source}}),
                        mcp::ArgError);
        CHECK_FALSE(fs::exists(assets / "Evil.mt"));
        CHECK_FALSE(fs::exists(assets / "scripts" / "lib" / "engine" / "Evil.mt"));

        fs::remove_all(assets);
    }
}
