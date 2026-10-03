#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"
#include "protocol/ArgReader.hpp"
#include "protocol/ResourceRegistry.hpp"
#include "tools/CoreTools.hpp"

#include "events/EventDispatcher.hpp"
#include "events/project/ProjectEvents.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include "events/scene/PluginComponentEvents.hpp"
#include "print/Log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
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

    void writeFile(const fs::path& path, const std::string& text)
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream << text;
    }

    void registerProject(const fs::path& assets)
    {
        events::EventDispatcher::instance().registerQueryHandler<events::project::GetCurrentProjectQuery>(
            [assets](const events::project::GetCurrentProjectQuery&) -> std::optional<config::ProjectConfig>
            {
                config::ProjectConfig config;
                config.projectName = "McpResources";
                config.workingDirectory = assets.string();
                return config;
            });
    }

    bool contains(const std::string& text, const std::string& needle)
    {
        return text.find(needle) != std::string::npos;
    }

    struct ResourceFixture
    {
        DispatcherScope dispatcherScope;
        mcp::MainThreadQueue queue;
        mcp::ResourceRegistry registry;

        ResourceFixture()
        {
            mcp::tools::registerCoreResources(registry, queue);
        }

        // Worker-affinity readers block on runOnMain; play the editor main thread here.
        template <typename TResult>
        TResult onWorker(std::function<TResult()> work)
        {
            auto future = std::async(std::launch::async, std::move(work));
            while (future.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready)
            {
                queue.drain(std::chrono::milliseconds(8));
            }
            return future.get();
        }

        mcp::ResourceContents readExact(const char* uri)
        {
            const mcp::ResourceDef* resource = registry.findExact(uri);
            REQUIRE(resource != nullptr);
            if (resource->affinity == mcp::ThreadAffinity::Main)
            {
                return resource->reader();
            }
            auto reader = resource->reader;
            return onWorker<mcp::ResourceContents>([reader]() { return reader(); });
        }

        mcp::ResourceContents readTemplate(const std::string& uri)
        {
            const mcp::ResourceTemplateDef* resourceTemplate = registry.findTemplate(uri);
            REQUIRE(resourceTemplate != nullptr);
            REQUIRE(resourceTemplate->affinity == mcp::ThreadAffinity::Worker);
            auto suffix = mcp::ResourceRegistry::percentDecode(std::string_view(uri).substr(resourceTemplate->prefix.size()));
            REQUIRE(suffix.has_value());
            auto reader = resourceTemplate->reader;
            const std::string decoded = *suffix;
            return onWorker<mcp::ResourceContents>([reader, uri, decoded]() { return reader(uri, decoded); });
        }

        std::vector<std::string> listContributed()
        {
            REQUIRE(registry.contributors().size() == 1);
            auto list = registry.contributors()[0].list;
            std::vector<mcp::ResourceDef> defs = onWorker<std::vector<mcp::ResourceDef>>([list]() { return list(); });
            std::vector<std::string> uris;
            for (const mcp::ResourceDef& def : defs)
            {
                CHECK(def.mimeType == "text/x-mtype");
                uris.push_back(def.uri);
            }
            return uris;
        }
    };
}

TEST_SUITE("MCP content resources")
{
    TEST_CASE("registerCoreResources registers the static resources, templates and the script listing")
    {
        ResourceFixture fixture;

        std::vector<std::string> uris;
        for (const mcp::ResourceDef& resource : fixture.registry.resources())
        {
            uris.push_back(resource.uri);
            CHECK(resource.reader);
            CHECK_FALSE(resource.mimeType.empty());
        }
        std::sort(uris.begin(), uris.end());
        CHECK(uris == std::vector<std::string>{
            "vf://docs/components", "vf://docs/mtype-api", "vf://logs", "vf://scene/hierarchy"
        });
        CHECK(fixture.registry.findExact("vf://logs")->affinity == mcp::ThreadAffinity::Worker);
        CHECK(fixture.registry.findExact("vf://scene/hierarchy")->affinity == mcp::ThreadAffinity::Main);

        REQUIRE(fixture.registry.templates().size() == 2);
        const mcp::ResourceTemplateDef* scripts = fixture.registry.findTemplate("vf://scripts/game/A.mt");
        REQUIRE(scripts != nullptr);
        CHECK(scripts->uriTemplate == "vf://scripts/{+path}");
        CHECK(scripts->mimeType == "text/x-mtype");
        const mcp::ResourceTemplateDef* module = fixture.registry.findTemplate("vf://docs/mtype-api/engine/Physics");
        REQUIRE(module != nullptr);
        CHECK(module->uriTemplate == "vf://docs/mtype-api/{+module}");

        // The exact resource wins over the template prefix.
        CHECK(fixture.registry.findExact("vf://docs/mtype-api") != nullptr);
        CHECK(fixture.registry.findTemplate("vf://docs/mtype-api") == nullptr);
        CHECK(fixture.registry.contributors().size() == 1);
    }

    TEST_CASE("vf://scripts reads game and lib scripts and rejects paths outside the root")
    {
        ResourceFixture fixture;
        const fs::path assets = makeTestDirectory("VertexForge_McpResources_Scripts");
        writeFile(assets / "scripts" / "game" / "Hello.mt", "@Script\npublic class Hello { }\n");
        writeFile(assets / "scripts" / "game" / "sub dir" / "A B.mt", "// spaced\n");
        writeFile(assets / "scripts" / "lib" / "engine" / "Log.mt", "public class Log { }\n");
        writeFile(assets / "secret.mt", "outside\n");
        registerProject(assets);

        const mcp::ResourceContents hello = fixture.readTemplate("vf://scripts/game/Hello.mt");
        CHECK(hello.uri == "vf://scripts/game/Hello.mt");
        CHECK(hello.mimeType == "text/x-mtype");
        CHECK(hello.text == "@Script\npublic class Hello { }\n");

        CHECK(fixture.readTemplate("vf://scripts/lib/engine/Log.mt").text == "public class Log { }\n");
        CHECK(fixture.readTemplate("vf://scripts/game/sub%20dir/A%20B.mt").text == "// spaced\n");

        CHECK_THROWS_AS(fixture.readTemplate("vf://scripts/../secret.mt"), mcp::ArgError);
        CHECK_THROWS_AS(fixture.readTemplate("vf://scripts/game/..%2F..%2Fsecret.mt"), mcp::ArgError);
        CHECK_THROWS_AS(fixture.readTemplate("vf://scripts//etc/passwd.mt"), mcp::ArgError);
        CHECK_THROWS_AS(fixture.readTemplate("vf://scripts/C:/Windows/win.mt"), mcp::ArgError);
        CHECK_THROWS_AS(fixture.readTemplate("vf://scripts/" + (assets / "secret.mt").generic_string()),
                        mcp::ArgError);
        CHECK_THROWS_AS(fixture.readTemplate("vf://scripts/game/Missing.mt"), mcp::ResourceNotFound);

        const std::vector<std::string> listed = fixture.listContributed();
        CHECK(listed == std::vector<std::string>{
            "vf://scripts/game/Hello.mt", "vf://scripts/game/sub%20dir/A%20B.mt"
        });

        fs::remove_all(assets);
    }

    TEST_CASE("vf://logs formats the newest 500 console lines as [seq][level] message")
    {
        ResourceFixture fixture;

        for (int i = 0; i < 520; ++i)
        {
            util::appendToConsoleBuffer("McpResourceTest filler " + std::to_string(i), util::LogLevel::Info);
        }
        util::appendToConsoleBuffer("McpResourceTest marker", util::LogLevel::Warning);
        uint64_t markerSeq = 0;
        {
            std::lock_guard<std::mutex> lock(util::imguiConsoleBufferMutex);
            markerSeq = util::imguiConsoleBuffer.back().sequenceNumber;
        }

        const mcp::ResourceContents logs = fixture.readExact("vf://logs");
        CHECK(logs.mimeType == "text/plain");
        CHECK(std::count(logs.text.begin(), logs.text.end(), '\n') == 500);
        const std::string lastLine = "[" + std::to_string(markerSeq) + "][warning] McpResourceTest marker\n";
        REQUIRE(logs.text.size() >= lastLine.size());
        CHECK(logs.text.substr(logs.text.size() - lastLine.size()) == lastLine);
        CHECK(contains(logs.text, "[" + std::to_string(markerSeq - 1) + "][info] McpResourceTest filler 519\n"));
        CHECK_FALSE(contains(logs.text, "McpResourceTest filler 20\n"));
    }

    TEST_CASE("vf://scene/hierarchy serves the scene_get_hierarchy JSON")
    {
        ResourceFixture fixture;
        events::EventDispatcher::instance().registerQueryHandler<events::scene::GetSceneHierarchyQuery>(
            [](const events::scene::GetSceneHierarchyQuery&)
            {
                services::SceneHierarchyData hierarchy;
                hierarchy.root.id = 100;
                services::EntityData root;
                root.handle.id = 100;
                services::EntityHandle child;
                child.id = 1;
                root.children.push_back(child);
                services::EntityData floor;
                floor.handle.id = 1;
                floor.name = "Floor";
                floor.parent = root.handle;
                hierarchy.entities = {root, floor};
                return hierarchy;
            });

        const mcp::ResourceContents contents = fixture.readExact("vf://scene/hierarchy");
        CHECK(contents.mimeType == "application/json");
        const nlohmann::json json = nlohmann::json::parse(contents.text);
        CHECK(json["root"] == 100);
        CHECK(json["entityCount"] == 1);
        REQUIRE(json["entities"].size() == 1);
        CHECK(json["entities"][0]["name"] == "Floor");
        CHECK(json["entities"][0].contains("position"));
    }

    TEST_CASE("vf://docs/components lists built-in and plugin components")
    {
        ResourceFixture fixture;

        const std::string withoutPlugins = fixture.readExact("vf://docs/components").text;
        CHECK(contains(withoutPlugins, "### Mesh\n"));
        CHECK(contains(withoutPlugins, "### RigidBody\n"));
        CHECK(contains(withoutPlugins, "_Plugin component types are unavailable._"));

        events::EventDispatcher::instance().registerQueryHandler<events::scene::GetPluginComponentTypesQuery>(
            [](const events::scene::GetPluginComponentTypesQuery&)
            {
                return nlohmann::json::array({
                    {{"name", "Health"}, {"plugin", "RTS"},
                     {"fields", nlohmann::json::array({
                         {{"name", "hp"}, {"type", "float"}, {"min", 0}, {"max", 100}},
                         {{"name", "id"}, {"type", "int"}, {"readOnly", true}}
                     })}}
                });
            });
        const std::string withPlugins = fixture.readExact("vf://docs/components").text;
        CHECK(contains(withPlugins, "### Health (plugin RTS)\n"));
        CHECK(contains(withPlugins, "- `hp`: float [0..100]\n"));
        CHECK(contains(withPlugins, "- `id`: int (read-only)\n"));
    }

    TEST_CASE("vf://docs/mtype-api degrades to the primer and the module template validates ids")
    {
        ResourceFixture fixture;

        // No project handler: the primer is still served.
        const mcp::ResourceContents noProject = fixture.readExact("vf://docs/mtype-api");
        CHECK(noProject.mimeType == "text/markdown");
        CHECK(contains(noProject.text, "# VertexForge mType scripting primer"));
        CHECK(contains(noProject.text, "signatures unavailable: no project is loaded"));

        const fs::path assets = makeTestDirectory("VertexForge_McpResources_MType");
        writeFile(assets / "scripts" / "game" / "Hello.mt", "// game only\n");
        registerProject(assets);
        CHECK(contains(fixture.readExact("vf://docs/mtype-api").text,
                       "signatures unavailable: project has no scripts/lib"));
        CHECK_THROWS_AS(fixture.readTemplate("vf://docs/mtype-api/engine/Physics"), mcp::ResourceNotFound);

        writeFile(assets / "scripts" / "lib" / "engine" / "Physics.mt",
                  "public class Physics {\n    public static function gravity(): float { return 9.81; }\n}\n");
        const mcp::ResourceContents module = fixture.readTemplate("vf://docs/mtype-api/engine/Physics");
        CHECK(module.mimeType == "text/markdown");
        CHECK(contains(module.text, "public static function gravity(): float;"));
        CHECK(contains(module.text, "`import * from \"../lib/engine/Physics.mt\";`"));

        const std::string full = fixture.readExact("vf://docs/mtype-api").text;
        CHECK(contains(full, "- `engine/Physics`: Physics: "));

        CHECK_THROWS_AS(fixture.readTemplate("vf://docs/mtype-api/../game/Hello"), mcp::ArgError);
        CHECK_THROWS_AS(fixture.readTemplate("vf://docs/mtype-api/engine/Nope"), mcp::ResourceNotFound);

        fs::remove_all(assets);
    }
}
