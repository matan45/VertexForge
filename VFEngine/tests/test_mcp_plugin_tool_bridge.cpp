#include <doctest.h>

#include "protocol/ToolRegistry.hpp"
#include "tools/PluginToolBridge.hpp"

#include "events/EventDispatcher.hpp"
#include "events/editor/PluginMcpToolEvents.hpp"

#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using events::editor::InvokePluginMcpToolCommand;
    using events::editor::ListPluginMcpToolsQuery;
    using events::editor::PluginMcpToolInfo;
    using events::editor::PluginMcpToolInvokeResult;
    using events::editor::PluginMcpToolsChangedNotification;

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

    PluginMcpToolInfo toolInfo(std::string qualifiedName, std::string plugin = "PluginAPITest")
    {
        PluginMcpToolInfo info;
        info.qualifiedName = std::move(qualifiedName);
        info.plugin = std::move(plugin);
        info.title = "Title " + info.qualifiedName;
        info.description = "does things";
        info.inputSchema = nlohmann::json{{"type", "object"}, {"properties", {{"text", {{"type", "string"}}}}}};
        info.readOnly = true;
        info.timeoutMs = 2500;
        return info;
    }

    // Fake Editor side: the PluginMcpToolHandler pair over a plain vector.
    struct BridgeFixture
    {
        DispatcherScope dispatcherScope;
        mcp::ToolRegistry registry;

        std::vector<PluginMcpToolInfo> tools;
        std::vector<InvokePluginMcpToolCommand> invocations;
        PluginMcpToolInvokeResult nextResult;
        int listQueries = 0;

        int notifications = 0;
        std::string lastNotification;
        mcp::tools::PluginToolBridge bridge{registry, [this](const std::string& json) {
            ++notifications;
            lastNotification = json;
        }};

        void registerHandlers()
        {
            auto& dispatcher = events::EventDispatcher::instance();
            dispatcher.registerQueryHandler<ListPluginMcpToolsQuery>(
                [this](const ListPluginMcpToolsQuery&) -> std::vector<PluginMcpToolInfo>
                {
                    ++listQueries;
                    return tools;
                });
            dispatcher.registerCommandHandler<InvokePluginMcpToolCommand>(
                [this](const InvokePluginMcpToolCommand& command) -> PluginMcpToolInvokeResult
                {
                    invocations.push_back(command);
                    return nextResult;
                });
        }
    };
}

TEST_CASE("PluginToolBridge: first sync mirrors the plugin tools and notifies once")
{
    BridgeFixture fixture;
    fixture.registerHandlers();
    fixture.tools = {toolInfo("pluginapitest_echo"), toolInfo("other_tool", "Other")};

    CHECK(fixture.bridge.isDirty());  // starts dirty: plugins load before the server
    CHECK(fixture.bridge.syncIfDirty());
    CHECK_FALSE(fixture.bridge.isDirty());
    CHECK(fixture.notifications == 1);
    CHECK(fixture.lastNotification == R"({"jsonrpc":"2.0","method":"notifications/tools/list_changed"})");
    CHECK(fixture.registry.size() == 2);

    auto tool = fixture.registry.find("pluginapitest_echo");
    REQUIRE(tool != nullptr);
    CHECK(tool->owner == "plugins");
    CHECK(tool->title == "Title pluginapitest_echo");
    CHECK(tool->description == "[plugin PluginAPITest] does things");
    CHECK(tool->inputSchema["properties"]["text"]["type"] == "string");
    CHECK(tool->readOnly);
    CHECK_FALSE(tool->destructive);
    CHECK(tool->timeout == std::chrono::milliseconds(2500));
    CHECK(tool->affinity == mcp::ThreadAffinity::Main);

    // Not dirty: no query, no notification.
    CHECK_FALSE(fixture.bridge.syncIfDirty());
    CHECK(fixture.listQueries == 1);
    CHECK(fixture.notifications == 1);
}

TEST_CASE("PluginToolBridge: change notification marks dirty and resyncs")
{
    BridgeFixture fixture;
    fixture.registerHandlers();
    fixture.bridge.subscribe();
    fixture.tools = {toolInfo("pluginapitest_echo")};
    REQUIRE(fixture.bridge.syncIfDirty());
    REQUIRE(fixture.notifications == 1);

    fixture.tools = {toolInfo("pluginapitest_echo"), toolInfo("pluginapitest_second")};
    events::EventDispatcher::instance().publish(PluginMcpToolsChangedNotification{});
    events::EventDispatcher::instance().publish(PluginMcpToolsChangedNotification{});  // a burst coalesces
    CHECK(fixture.bridge.isDirty());

    CHECK(fixture.bridge.syncIfDirty());
    CHECK(fixture.notifications == 2);
    CHECK(fixture.registry.find("pluginapitest_second") != nullptr);

    // Unchanged listing: re-queried, but no list_changed.
    events::EventDispatcher::instance().publish(PluginMcpToolsChangedNotification{});
    CHECK_FALSE(fixture.bridge.syncIfDirty());
    CHECK(fixture.notifications == 2);

    // Tool gone (plugin deactivated / unloaded).
    fixture.tools.clear();
    events::EventDispatcher::instance().publish(PluginMcpToolsChangedNotification{});
    CHECK(fixture.bridge.syncIfDirty());
    CHECK(fixture.notifications == 3);
    CHECK(fixture.registry.find("pluginapitest_echo") == nullptr);
    CHECK(fixture.registry.size() == 0);

    // After unsubscribe, notifications no longer mark dirty.
    fixture.bridge.unsubscribe();
    events::EventDispatcher::instance().publish(PluginMcpToolsChangedNotification{});
    CHECK_FALSE(fixture.bridge.isDirty());
}

TEST_CASE("PluginToolBridge: sync without notify does not call the callback")
{
    BridgeFixture fixture;
    fixture.registerHandlers();
    fixture.tools = {toolInfo("pluginapitest_echo")};

    CHECK(fixture.bridge.syncIfDirty(false));
    CHECK(fixture.notifications == 0);
    CHECK(fixture.registry.find("pluginapitest_echo") != nullptr);
}

TEST_CASE("PluginToolBridge: missing list handler is not an error")
{
    BridgeFixture fixture;  // no handlers registered (e.g. Runtime / tests)

    bool changed = true;
    CHECK_NOTHROW(changed = fixture.bridge.syncIfDirty());
    CHECK_FALSE(changed);
    CHECK(fixture.notifications == 0);
    CHECK(fixture.registry.size() == 0);
}

TEST_CASE("PluginToolBridge: generated handler routes to InvokePluginMcpToolCommand")
{
    BridgeFixture fixture;
    fixture.registerHandlers();
    fixture.tools = {toolInfo("pluginapitest_echo")};
    REQUIRE(fixture.bridge.syncIfDirty());

    auto tool = fixture.registry.find("pluginapitest_echo");
    REQUIRE(tool != nullptr);

    SUBCASE("success maps structured + text")
    {
        fixture.nextResult.structured = nlohmann::json{{"echo", "hi"}};
        fixture.nextResult.text = "said hi";
        auto result = tool->handler(nlohmann::json{{"text", "hi"}});

        REQUIRE(fixture.invocations.size() == 1);
        CHECK(fixture.invocations[0].qualifiedName == "pluginapitest_echo");
        CHECK(fixture.invocations[0].arguments["text"] == "hi");
        CHECK_FALSE(result.isError);
        CHECK(result.structured["echo"] == "hi");
        CHECK(result.text == "said hi");
    }

    SUBCASE("isError maps to ToolResult::error")
    {
        fixture.nextResult.isError = true;
        fixture.nextResult.text = "bad text";
        auto result = tool->handler(nlohmann::json::object());
        CHECK(result.isError);
        CHECK(result.text == "bad text");
    }

    SUBCASE("isError without text gets a generic message")
    {
        fixture.nextResult.isError = true;
        auto result = tool->handler(nlohmann::json::object());
        CHECK(result.isError);
        CHECK(result.text == "plugin tool failed");
    }
}

TEST_CASE("PluginToolBridge: a plugin tool cannot shadow a core tool")
{
    BridgeFixture fixture;
    fixture.registerHandlers();

    mcp::ToolDef core;
    core.name = "scene_list";
    core.description = "core tool";
    core.handler = [](const nlohmann::json&) { return mcp::ToolResult::ok(nlohmann::json{{"core", true}}); };
    fixture.registry.add(core);

    fixture.tools = {toolInfo("scene_list"), toolInfo("pluginapitest_echo")};
    CHECK(fixture.bridge.syncIfDirty());

    auto shadowed = fixture.registry.find("scene_list");
    REQUIRE(shadowed != nullptr);
    CHECK(shadowed->owner.empty());
    CHECK(shadowed->description == "core tool");
    CHECK(fixture.registry.find("pluginapitest_echo") != nullptr);
    CHECK(fixture.registry.size() == 2);
}
