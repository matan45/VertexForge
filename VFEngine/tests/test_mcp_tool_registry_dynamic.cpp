#include <doctest.h>

#include "protocol/ToolRegistry.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace
{
    mcp::ToolDef makeTool(const std::string& name, const std::string& description = "d")
    {
        mcp::ToolDef tool;
        tool.name = name;
        tool.description = description;
        tool.handler = [name](const nlohmann::json&)
        {
            return mcp::ToolResult::ok({{"tool", name}});
        };
        return tool;
    }

    std::vector<std::string> listedNames(const mcp::ToolRegistry& registry)
    {
        std::vector<std::string> names;
        for (const nlohmann::json& entry : registry.listJson())
        {
            names.push_back(entry["name"].get<std::string>());
        }
        return names;
    }
}

TEST_SUITE("McpToolRegistryDynamic")
{
    TEST_CASE("replaceGroup appends the group after core tools and swaps it atomically")
    {
        mcp::ToolRegistry registry;
        registry.add(makeTool("core_a"));
        registry.add(makeTool("core_b"));

        CHECK(registry.replaceGroup("plugins", {makeTool("p_one"), makeTool("p_two")}));
        CHECK(listedNames(registry) == std::vector<std::string>{"core_a", "core_b", "p_one", "p_two"});
        REQUIRE(registry.find("p_one"));
        CHECK(registry.find("p_one")->owner == "plugins");

        // Core tools added later land before a re-applied group: the group is
        // removed and appended at the end in its new order.
        registry.add(makeTool("core_c"));
        CHECK(registry.replaceGroup("plugins", {makeTool("p_two"), makeTool("p_three")}));
        CHECK(listedNames(registry) == std::vector<std::string>{"core_a", "core_b", "core_c", "p_two", "p_three"});
        CHECK_FALSE(registry.find("p_one"));
        CHECK(registry.size() == 5);

        CHECK(registry.replaceGroup("plugins", {}));
        CHECK(listedNames(registry) == std::vector<std::string>{"core_a", "core_b", "core_c"});
        CHECK_FALSE(registry.find("p_two"));
    }

    TEST_CASE("replaceGroup never shadows another owner's tool")
    {
        mcp::ToolRegistry registry;
        registry.add(makeTool("scene_new", "core version"));

        std::vector<std::string> rejected;
        registry.replaceGroup("plugins", {makeTool("scene_new", "evil"), makeTool("p_ok"), makeTool("p_ok", "dup")},
                              &rejected);

        CHECK(rejected == std::vector<std::string>{"scene_new", "p_ok"});
        REQUIRE(registry.find("scene_new"));
        CHECK(registry.find("scene_new")->description == "core version");
        CHECK(registry.find("scene_new")->owner.empty());
        REQUIRE(registry.find("p_ok"));
        CHECK(registry.find("p_ok")->description == "d");
        CHECK(registry.size() == 2);

        // A second group cannot take a name the first group owns either.
        rejected.clear();
        registry.replaceGroup("other", {makeTool("p_ok")}, &rejected);
        CHECK(rejected == std::vector<std::string>{"p_ok"});
        CHECK(registry.find("p_ok")->owner == "plugins");
    }

    TEST_CASE("revision bumps only when the listing changes")
    {
        mcp::ToolRegistry registry;
        registry.add(makeTool("core_a"));
        const uint64_t afterAdd = registry.revision();

        CHECK(registry.replaceGroup("plugins", {makeTool("p_one")}));
        const uint64_t afterFirst = registry.revision();
        CHECK(afterFirst > afterAdd);

        // Same metadata (new handler objects) -> no change.
        CHECK_FALSE(registry.replaceGroup("plugins", {makeTool("p_one")}));
        CHECK(registry.revision() == afterFirst);

        // Description change -> change.
        CHECK(registry.replaceGroup("plugins", {makeTool("p_one", "new description")}));
        CHECK(registry.revision() > afterFirst);
        const uint64_t afterDescription = registry.revision();

        // Annotation change -> change.
        mcp::ToolDef readOnly = makeTool("p_one", "new description");
        readOnly.readOnly = true;
        CHECK(registry.replaceGroup("plugins", {readOnly}));
        CHECK(registry.revision() > afterDescription);

        // Only rejected defs for an empty group -> no change.
        const uint64_t beforeRejected = registry.revision();
        CHECK_FALSE(registry.replaceGroup("other", {makeTool("core_a")}));
        CHECK(registry.revision() == beforeRejected);
    }

    TEST_CASE("a found tool stays valid after its group is removed")
    {
        mcp::ToolRegistry registry;
        registry.replaceGroup("plugins", {makeTool("p_one")});

        std::shared_ptr<const mcp::ToolDef> held = registry.find("p_one");
        REQUIRE(held);
        registry.replaceGroup("plugins", {});
        CHECK_FALSE(registry.find("p_one"));

        CHECK(held->name == "p_one");
        mcp::ToolResult result = held->handler(nlohmann::json::object());
        CHECK(result.structured["tool"] == "p_one");
    }

    TEST_CASE("a moved registry keeps its tools and stays usable")
    {
        mcp::ToolRegistry source;
        source.add(makeTool("core_a"));
        source.replaceGroup("plugins", {makeTool("p_one")});

        mcp::ToolRegistry moved(std::move(source));
        CHECK(moved.size() == 2);
        REQUIRE(moved.find("p_one"));

        mcp::ToolRegistry assigned;
        assigned = std::move(moved);
        CHECK(assigned.size() == 2);
        CHECK(assigned.replaceGroup("plugins", {}));
        CHECK(assigned.size() == 1);
    }

    TEST_CASE("concurrent find and replaceGroup do not race")
    {
        mcp::ToolRegistry registry;
        registry.add(makeTool("core_a"));

        std::atomic<bool> stop{false};
        std::atomic<int> missingCore{0};
        std::thread reader([&]()
        {
            while (!stop.load())
            {
                if (std::shared_ptr<const mcp::ToolDef> tool = registry.find("p_one"))
                {
                    tool->handler(nlohmann::json::object());
                }
                // doctest assertions stay on the test thread.
                if (!registry.find("core_a"))
                {
                    missingCore.fetch_add(1);
                }
                (void)registry.listJson();
            }
        });

        for (int i = 0; i < 500; ++i)
        {
            if (i % 2 == 0)
            {
                registry.replaceGroup("plugins", {makeTool("p_one"), makeTool("p_two")});
            }
            else
            {
                registry.replaceGroup("plugins", {});
            }
        }
        stop.store(true);
        reader.join();

        CHECK(missingCore.load() == 0);
        CHECK(registry.size() == 1);
    }
}
