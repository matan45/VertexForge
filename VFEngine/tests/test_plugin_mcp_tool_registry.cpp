#include <doctest.h>

#include "core/PluginMcpToolRegistry.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace
{
    using plugin::PluginMcpToolDesc;
    using plugin::PluginMcpToolRegistry;
    using plugin::PluginMcpToolResult;

    struct RegistryScope
    {
        RegistryScope() { PluginMcpToolRegistry::clearForTests(); }
        ~RegistryScope() { PluginMcpToolRegistry::clearForTests(); }
    };

    PluginMcpToolDesc makeTool(std::string name, std::string reply = "ok")
    {
        PluginMcpToolDesc desc;
        desc.name = std::move(name);
        desc.description = "test tool";
        desc.handler = [reply](const nlohmann::json& args) {
            return PluginMcpToolResult::ok(nlohmann::json{{"reply", reply}, {"args", args}});
        };
        return desc;
    }

    bool listed(const std::string& qualifiedName)
    {
        for (const auto& info : PluginMcpToolRegistry::listActive())
        {
            if (info.qualifiedName == qualifiedName) return true;
        }
        return false;
    }
}

TEST_CASE("PluginMcpToolRegistry: qualify lower-cases and sanitises")
{
    CHECK(PluginMcpToolRegistry::qualify("PluginAPITest", "echo") == "pluginapitest_echo");
    CHECK(PluginMcpToolRegistry::qualify("My Plugin-2", "do_it") == "my_plugin_2_do_it");
    CHECK(PluginMcpToolRegistry::qualify("__Weird!!Name__", "x") == "weird_name_x");
    CHECK(PluginMcpToolRegistry::qualify("a", "b__c_") == "a_b_c");
    CHECK(PluginMcpToolRegistry::qualify("", "tool") == "tool");
}

TEST_CASE("PluginMcpToolRegistry: add registers an active tool and invoke runs it")
{
    RegistryScope scope;

    auto outcome = PluginMcpToolRegistry::add("PluginAPITest", makeTool("echo"));
    REQUIRE(outcome.ok);
    CHECK_FALSE(outcome.replaced);
    CHECK(outcome.qualifiedName == "pluginapitest_echo");
    CHECK(PluginMcpToolRegistry::hasTools("PluginAPITest"));

    auto list = PluginMcpToolRegistry::listActive();
    REQUIRE(list.size() == 1);
    CHECK(list[0].qualifiedName == "pluginapitest_echo");
    CHECK(list[0].plugin == "PluginAPITest");
    CHECK(list[0].description == "test tool");
    CHECK(list[0].inputSchema["type"] == "object");

    auto result = PluginMcpToolRegistry::invoke("pluginapitest_echo", nlohmann::json{{"text", "hi"}});
    CHECK_FALSE(result.isError);
    CHECK(result.structured["reply"] == "ok");
    CHECK(result.structured["args"]["text"] == "hi");

    auto unknown = PluginMcpToolRegistry::invoke("nope_tool", nlohmann::json::object());
    CHECK(unknown.isError);
    CHECK(unknown.text.find("no longer available") != std::string::npos);
}

TEST_CASE("PluginMcpToolRegistry: tool name validation")
{
    RegistryScope scope;

    CHECK_FALSE(PluginMcpToolRegistry::add("P", makeTool("")).ok);
    CHECK_FALSE(PluginMcpToolRegistry::add("P", makeTool("Echo")).ok);
    CHECK_FALSE(PluginMcpToolRegistry::add("P", makeTool("with-dash")).ok);
    CHECK_FALSE(PluginMcpToolRegistry::add("P", makeTool("with space")).ok);
    CHECK_FALSE(PluginMcpToolRegistry::add("P", makeTool("__")).ok);
    CHECK_FALSE(PluginMcpToolRegistry::add("P", makeTool(std::string(33, 'a'))).ok);
    CHECK(PluginMcpToolRegistry::add("P", makeTool(std::string(32, 'a'))).ok);
    CHECK(PluginMcpToolRegistry::add("P", makeTool("snake_case_9")).ok);

    // Plugin name without any alphanumerics.
    CHECK_FALSE(PluginMcpToolRegistry::add("!!!", makeTool("tool")).ok);

    // Qualified name longer than 48 characters.
    const std::string longPlugin(20, 'p');
    auto tooLong = PluginMcpToolRegistry::add(longPlugin, makeTool(std::string(28, 't')));
    CHECK_FALSE(tooLong.ok);
    CHECK(tooLong.error.find("48") != std::string::npos);

    // Empty handler.
    PluginMcpToolDesc noHandler = makeTool("nohandler");
    noHandler.handler = nullptr;
    CHECK_FALSE(PluginMcpToolRegistry::add("P", std::move(noHandler)).ok);
}

TEST_CASE("PluginMcpToolRegistry: inputSchema must be an object schema")
{
    RegistryScope scope;

    PluginMcpToolDesc arraySchema = makeTool("a");
    arraySchema.inputSchema = nlohmann::json::array();
    CHECK_FALSE(PluginMcpToolRegistry::add("P", std::move(arraySchema)).ok);

    PluginMcpToolDesc noType = makeTool("b");
    noType.inputSchema = nlohmann::json{{"properties", nlohmann::json::object()}};
    CHECK_FALSE(PluginMcpToolRegistry::add("P", std::move(noType)).ok);

    PluginMcpToolDesc wrongType = makeTool("c");
    wrongType.inputSchema = nlohmann::json{{"type", "string"}};
    CHECK_FALSE(PluginMcpToolRegistry::add("P", std::move(wrongType)).ok);

    PluginMcpToolDesc numericType = makeTool("d");
    numericType.inputSchema = nlohmann::json{{"type", 1}};
    CHECK_FALSE(PluginMcpToolRegistry::add("P", std::move(numericType)).ok);

    CHECK(PluginMcpToolRegistry::listActive().empty());
}

TEST_CASE("PluginMcpToolRegistry: timeout is clamped to [1s, 120s]")
{
    RegistryScope scope;

    PluginMcpToolDesc fast = makeTool("fast");
    fast.timeoutMs = 5;
    PluginMcpToolDesc slow = makeTool("slow");
    slow.timeoutMs = 600000;
    PluginMcpToolDesc normal = makeTool("normal");
    normal.timeoutMs = 30000;
    REQUIRE(PluginMcpToolRegistry::add("P", std::move(fast)).ok);
    REQUIRE(PluginMcpToolRegistry::add("P", std::move(slow)).ok);
    REQUIRE(PluginMcpToolRegistry::add("P", std::move(normal)).ok);

    auto list = PluginMcpToolRegistry::listActive();
    REQUIRE(list.size() == 3);
    CHECK(list[0].timeoutMs == 1000);
    CHECK(list[1].timeoutMs == 120000);
    CHECK(list[2].timeoutMs == 30000);
}

TEST_CASE("PluginMcpToolRegistry: qualified-name collisions")
{
    RegistryScope scope;

    // "a_b" + "c" and "a" + "b_c" both qualify to "a_b_c".
    REQUIRE(PluginMcpToolRegistry::add("a_b", makeTool("c")).ok);
    auto clash = PluginMcpToolRegistry::add("a", makeTool("b_c"));
    CHECK_FALSE(clash.ok);
    CHECK(clash.error.find("a_b_c") != std::string::npos);

    // Same plugin, different tool name collapsing to the same qualified name.
    CHECK_FALSE(PluginMcpToolRegistry::add("a_b", makeTool("c__")).ok);

    // Same plugin + same tool replaces in place.
    auto replaced = PluginMcpToolRegistry::add("a_b", makeTool("c", "second"));
    CHECK(replaced.ok);
    CHECK(replaced.replaced);
    auto list = PluginMcpToolRegistry::listActive();
    REQUIRE(list.size() == 1);
    CHECK(list[0].plugin == "a_b");
    CHECK(PluginMcpToolRegistry::invoke("a_b_c", nlohmann::json::object()).structured["reply"] == "second");
}

TEST_CASE("PluginMcpToolRegistry: inactive plugins are hidden and refuse invoke")
{
    RegistryScope scope;

    REQUIRE(PluginMcpToolRegistry::add("Alpha", makeTool("one")).ok);
    REQUIRE(PluginMcpToolRegistry::add("Beta", makeTool("two")).ok);

    CHECK(PluginMcpToolRegistry::setPluginActive("Alpha", false));
    CHECK_FALSE(listed("alpha_one"));
    CHECK(listed("beta_two"));
    auto refused = PluginMcpToolRegistry::invoke("alpha_one", nlohmann::json::object());
    CHECK(refused.isError);
    CHECK(refused.text.find("no longer available") != std::string::npos);

    // A plugin without tools reports that nothing changed.
    CHECK_FALSE(PluginMcpToolRegistry::setPluginActive("Gamma", false));

    CHECK(PluginMcpToolRegistry::setPluginActive("Alpha", true));
    CHECK(listed("alpha_one"));
    CHECK_FALSE(PluginMcpToolRegistry::invoke("alpha_one", nlohmann::json::object()).isError);
}

TEST_CASE("PluginMcpToolRegistry: setPluginActive before register is honoured")
{
    RegistryScope scope;

    CHECK_FALSE(PluginMcpToolRegistry::setPluginActive("Late", false));
    REQUIRE(PluginMcpToolRegistry::add("Late", makeTool("tool")).ok);
    CHECK(PluginMcpToolRegistry::hasTools("Late"));
    CHECK_FALSE(listed("late_tool"));
    CHECK(PluginMcpToolRegistry::invoke("late_tool", nlohmann::json::object()).isError);

    CHECK(PluginMcpToolRegistry::setPluginActive("Late", true));
    CHECK(listed("late_tool"));
}

TEST_CASE("PluginMcpToolRegistry: a handler may unregister itself during invoke")
{
    RegistryScope scope;

    int calls = 0;
    PluginMcpToolDesc desc = makeTool("once");
    desc.handler = [&calls](const nlohmann::json&) {
        ++calls;
        // Drops the registry's reference to this very std::function mid-call.
        CHECK(PluginMcpToolRegistry::remove("Self", "once"));
        // Re-entering the registry from a handler must not deadlock.
        CHECK_FALSE(PluginMcpToolRegistry::hasTools("Self"));
        return PluginMcpToolResult::ok(nlohmann::json{{"calls", calls}});
    };
    REQUIRE(PluginMcpToolRegistry::add("Self", std::move(desc)).ok);

    auto first = PluginMcpToolRegistry::invoke("self_once", nlohmann::json::object());
    CHECK_FALSE(first.isError);
    CHECK(first.structured["calls"] == 1);

    auto second = PluginMcpToolRegistry::invoke("self_once", nlohmann::json::object());
    CHECK(second.isError);
    CHECK(calls == 1);
}

TEST_CASE("PluginMcpToolRegistry: handler exceptions become isError results")
{
    RegistryScope scope;

    PluginMcpToolDesc throwsStd = makeTool("std");
    throwsStd.handler = [](const nlohmann::json&) -> PluginMcpToolResult {
        throw std::runtime_error("boom");
    };
    PluginMcpToolDesc throwsOther = makeTool("other");
    throwsOther.handler = [](const nlohmann::json&) -> PluginMcpToolResult {
        throw 42;
    };
    PluginMcpToolDesc reportsError = makeTool("reports");
    reportsError.handler = [](const nlohmann::json&) {
        return PluginMcpToolResult::error("bad input");
    };
    REQUIRE(PluginMcpToolRegistry::add("P", std::move(throwsStd)).ok);
    REQUIRE(PluginMcpToolRegistry::add("P", std::move(throwsOther)).ok);
    REQUIRE(PluginMcpToolRegistry::add("P", std::move(reportsError)).ok);

    auto stdResult = PluginMcpToolRegistry::invoke("p_std", nlohmann::json::object());
    CHECK(stdResult.isError);
    CHECK(stdResult.text.find("boom") != std::string::npos);

    auto otherResult = PluginMcpToolRegistry::invoke("p_other", nlohmann::json::object());
    CHECK(otherResult.isError);
    CHECK_FALSE(otherResult.text.empty());

    auto reported = PluginMcpToolRegistry::invoke("p_reports", nlohmann::json::object());
    CHECK(reported.isError);
    CHECK(reported.text == "bad input");
}

TEST_CASE("PluginMcpToolRegistry: remove and removeByPlugin")
{
    RegistryScope scope;

    REQUIRE(PluginMcpToolRegistry::add("Alpha", makeTool("one")).ok);
    REQUIRE(PluginMcpToolRegistry::add("Alpha", makeTool("two")).ok);
    REQUIRE(PluginMcpToolRegistry::add("Beta", makeTool("three")).ok);

    CHECK(PluginMcpToolRegistry::remove("Alpha", "one"));
    CHECK_FALSE(PluginMcpToolRegistry::remove("Alpha", "one"));
    CHECK_FALSE(PluginMcpToolRegistry::remove("Beta", "two"));  // tool of another plugin
    CHECK(listed("alpha_two"));

    CHECK(PluginMcpToolRegistry::removeByPlugin("Alpha"));
    CHECK_FALSE(PluginMcpToolRegistry::removeByPlugin("Alpha"));
    CHECK_FALSE(PluginMcpToolRegistry::hasTools("Alpha"));
    CHECK(listed("beta_three"));
    CHECK(PluginMcpToolRegistry::listActive().size() == 1);

    // Unload forgets the inactive state, so a reloaded plugin starts active.
    CHECK(PluginMcpToolRegistry::setPluginActive("Beta", false));
    CHECK(PluginMcpToolRegistry::removeByPlugin("Beta"));
    REQUIRE(PluginMcpToolRegistry::add("Beta", makeTool("three")).ok);
    CHECK(listed("beta_three"));
}
