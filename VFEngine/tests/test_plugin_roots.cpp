#include "doctest.h"

#include "core/PluginRoots.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// VK-1732: the pure rules behind the two plugin roots (header-only, so Tests does not link the Plugin
// module): the overrides file's format, which state a plugin loads with, and which of two same-named
// plugins wins.

using plugin::PluginOverrides;
using plugin::PluginOverridesStatus;
using plugin::PluginRootCandidate;
using plugin::PluginRootKind;

TEST_CASE("plugin roots: overrides file parses")
{
    SUBCASE("no file is Absent and writable")
    {
        const PluginOverrides overrides = plugin::parsePluginOverrides(std::nullopt);
        CHECK(overrides.status == PluginOverridesStatus::Absent);
        CHECK(overrides.enabled.empty());
        CHECK(overrides.writable());
    }

    SUBCASE("version 1 with names")
    {
        const PluginOverrides overrides =
            plugin::parsePluginOverrides(std::string_view(R"({"version":1,"enabled":{"PSDImport":true,"HexTerrain":false}})"));
        REQUIRE(overrides.status == PluginOverridesStatus::Ok);
        CHECK(overrides.writable());
        CHECK(overrides.enabled.size() == 2);
        CHECK(overrides.enabled.at("PSDImport"));
        CHECK_FALSE(overrides.enabled.at("HexTerrain"));
    }

    SUBCASE("a UTF-8 BOM and CRLF are accepted")
    {
        const PluginOverrides overrides =
            plugin::parsePluginOverrides(std::string_view("\xEF\xBB\xBF{\r\n  \"version\": 1,\r\n  \"enabled\": {\"A\": false}\r\n}\r\n"));
        REQUIRE(overrides.status == PluginOverridesStatus::Ok);
        CHECK_FALSE(overrides.enabled.at("A"));
    }

    SUBCASE("version 1 without \"enabled\" holds nothing")
    {
        const PluginOverrides overrides = plugin::parsePluginOverrides(std::string_view(R"({"version":1})"));
        CHECK(overrides.status == PluginOverridesStatus::Ok);
        CHECK(overrides.enabled.empty());
    }

    SUBCASE("a newer version is Newer: ignored and never written")
    {
        const PluginOverrides overrides =
            plugin::parsePluginOverrides(std::string_view(R"({"version":2,"enabled":{"A":true},"extra":[1]})"));
        CHECK(overrides.status == PluginOverridesStatus::Newer);
        CHECK(overrides.enabled.empty());
        CHECK_FALSE(overrides.writable());
        CHECK(overrides.problem.find("newer") != std::string::npos);
    }

    SUBCASE("anything else is Invalid: ignored and not overwritten")
    {
        for (const std::string_view text : {
                 std::string_view("not json"),
                 std::string_view("[1,2]"),
                 std::string_view(R"({"enabled":{"A":true}})"),          // no version
                 std::string_view(R"({"version":"1","enabled":{}})"),    // a string version
                 std::string_view(R"({"version":1.0,"enabled":{}})"),    // not an integer
                 std::string_view(R"({"version":0,"enabled":{}})"),
                 std::string_view(R"({"version":-1,"enabled":{}})"),
                 std::string_view(R"({"version":1,"enabled":[]})"),
                 std::string_view(R"({"version":1,"enabled":{"A":"yes"}})"),
                 std::string_view(R"({"version":1,"enabled":{"A":true,"B":1}})"),
                 std::string_view("")})
        {
            const std::string shown(text);
            CAPTURE(shown);
            const PluginOverrides overrides = plugin::parsePluginOverrides(text);
            CHECK(overrides.status == PluginOverridesStatus::Invalid);
            CHECK(overrides.enabled.empty());
            CHECK_FALSE(overrides.writable());
            CHECK_FALSE(overrides.problem.empty());
        }
    }
}

TEST_CASE("plugin roots: overrides file serializes and reads back")
{
    const std::map<std::string, bool> enabled = {{"Zeta", false}, {"Alpha", true}};
    const std::string text = plugin::serializePluginOverrides(enabled);

    // "version" first, names sorted, 4-space indent, a final newline
    CHECK(text == "{\n"
                  "    \"version\": 1,\n"
                  "    \"enabled\": {\n"
                  "        \"Alpha\": true,\n"
                  "        \"Zeta\": false\n"
                  "    }\n"
                  "}\n");

    const PluginOverrides back = plugin::parsePluginOverrides(std::string_view(text));
    REQUIRE(back.status == PluginOverridesStatus::Ok);
    CHECK(back.enabled == enabled);

    CHECK(plugin::serializePluginOverrides({}) == "{\n    \"version\": 1,\n    \"enabled\": {}\n}\n");
}

TEST_CASE("plugin roots: effective enabled state")
{
    const PluginOverrides overrides =
        plugin::parsePluginOverrides(std::string_view(R"({"version":1,"enabled":{"On":true,"Off":false}})"));
    REQUIRE(overrides.status == PluginOverridesStatus::Ok);

    SUBCASE("an override decides for a shipped plugin, both ways")
    {
        CHECK(plugin::effectiveEnabled(PluginRootKind::Engine, "On", false, &overrides));
        CHECK_FALSE(plugin::effectiveEnabled(PluginRootKind::Engine, "Off", true, &overrides));
    }

    SUBCASE("without an override the descriptor decides")
    {
        CHECK(plugin::effectiveEnabled(PluginRootKind::Engine, "Other", true, &overrides));
        CHECK_FALSE(plugin::effectiveEnabled(PluginRootKind::Engine, "Other", false, &overrides));
        CHECK(plugin::effectiveEnabled(PluginRootKind::Engine, "On", true, nullptr));
        CHECK_FALSE(plugin::effectiveEnabled(PluginRootKind::Engine, "On", false, nullptr));
    }

    SUBCASE("a user's plugin ignores the overrides file")
    {
        CHECK_FALSE(plugin::effectiveEnabled(PluginRootKind::User, "On", false, &overrides));
        CHECK(plugin::effectiveEnabled(PluginRootKind::User, "Off", true, &overrides));
    }

    SUBCASE("names compare exactly")
    {
        CHECK_FALSE(plugin::effectiveEnabled(PluginRootKind::Engine, "on", false, &overrides));
    }

    SUBCASE("a Newer or Invalid file counts as none")
    {
        const PluginOverrides newer =
            plugin::parsePluginOverrides(std::string_view(R"({"version":7,"enabled":{"On":true}})"));
        REQUIRE(newer.status == PluginOverridesStatus::Newer);
        CHECK_FALSE(plugin::effectiveEnabled(PluginRootKind::Engine, "On", false, &newer));

        PluginOverrides invalid;
        invalid.status = PluginOverridesStatus::Invalid;
        invalid.enabled["On"] = true;  // never consulted
        CHECK_FALSE(plugin::effectiveEnabled(PluginRootKind::Engine, "On", false, &invalid));
    }
}

TEST_CASE("plugin roots: shadowing")
{
    using Result = std::vector<std::optional<std::size_t>>;

    SUBCASE("distinct names shadow nothing")
    {
        const Result result = plugin::findShadowed({{"A", PluginRootKind::Engine}, {"B", PluginRootKind::User}});
        CHECK(result == Result{std::nullopt, std::nullopt});
    }

    SUBCASE("a shipped plugin wins over the user's with its name")
    {
        const Result result = plugin::findShadowed({{"A", PluginRootKind::Engine}, {"A", PluginRootKind::User}});
        CHECK(result == Result{std::nullopt, std::size_t{0}});
    }

    SUBCASE("the shipped plugin wins whatever the list order")
    {
        const Result result = plugin::findShadowed({{"A", PluginRootKind::User}, {"A", PluginRootKind::Engine}});
        CHECK(result == Result{std::size_t{1}, std::nullopt});
    }

    SUBCASE("within a root the first wins")
    {
        const Result engine = plugin::findShadowed({{"A", PluginRootKind::Engine},
                                                    {"A", PluginRootKind::Engine},
                                                    {"A", PluginRootKind::User}});
        CHECK(engine == Result{std::nullopt, std::size_t{0}, std::size_t{0}});

        const Result user = plugin::findShadowed({{"B", PluginRootKind::User}, {"B", PluginRootKind::User}});
        CHECK(user == Result{std::nullopt, std::size_t{0}});
    }

    SUBCASE("names compare exactly")
    {
        const Result result = plugin::findShadowed({{"A", PluginRootKind::Engine}, {"a", PluginRootKind::User}});
        CHECK(result == Result{std::nullopt, std::nullopt});
    }

    SUBCASE("an empty list")
    {
        CHECK(plugin::findShadowed({}).empty());
    }
}
