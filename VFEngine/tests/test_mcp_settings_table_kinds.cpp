#include <doctest.h>
#include <ostream> // doctest stringifies std::string and nlohmann::json through operator<<

#include "protocol/ArgReader.hpp"
#include "tools/SettingsTable.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>

// VK-1659: the field kinds the UI tools added to the settings tables (tools/SettingsTable.hpp): Vec2 and Vec4
// (arrays of exactly 2 / 4 numbers), Color ([r, g, b] with alpha 1, [r, g, b, a] or "#RRGGBB" / "#RRGGBBAA",
// always reported [r, g, b, a]) and StringList (min / max = the item count); their ranges and schema entries; and
// dropUnchanged, which lets every value a *_get reported pass back through strictPatch, even one outside its range.
namespace
{
    namespace env = mcp::tools::env;
    using json = nlohmann::json;

    struct KindsProbe
    {
        glm::vec2 offset{0.0f, 0.0f};
        glm::vec4 padding{0.0f};
        glm::vec4 tint{1.0f, 1.0f, 1.0f, 1.0f};
        std::vector<std::string> items;
        float speed = 1.0f;
        int count = 0;
        bool live = false;
    };

    env::FieldDesc<KindsProbe> itemsField()
    {
        env::FieldDesc<KindsProbe> field;
        field.key = "items";
        field.kind = env::FieldKind::StringList;
        field.min = 1.0;
        field.max = 3.0;
        field.help = "One to three names.";
        field.get = [](const KindsProbe& value) -> json { return value.items; };
        field.set = [](KindsProbe& value, const json& v) { value.items = v.get<std::vector<std::string>>(); };
        return field;
    }

    const env::SectionTable<KindsProbe>& probeTable()
    {
        static const env::SectionTable<KindsProbe> table = []
        {
            env::SectionTable<KindsProbe> t;
            t.name = "probe";
            t.fields = {
                env::vec2Field<KindsProbe, &KindsProbe::offset>("offset", -10.0, 10.0, "An offset."),
                env::vec4Field<KindsProbe, &KindsProbe::padding>("padding", 0.0, 100.0, "Padding."),
                env::colorField<KindsProbe, &KindsProbe::tint>("tint", "A tint."),
                itemsField(),
                env::floatField<KindsProbe, &KindsProbe::speed>("speed", 0.5, 2.0, "A speed."),
                env::readOnly(env::intField<KindsProbe, &KindsProbe::count>("count", 0.0, 10.0, "A count."),
                              "set by the engine"),
                env::liveOnly(env::boolField<KindsProbe, &KindsProbe::live>("live", "A debug view."), "a debug view")
            };
            return t;
        }();
        return table;
    }

    KindsProbe patched(const json& patch)
    {
        return env::strictPatch(probeTable(), KindsProbe{}, patch, "probe");
    }

    // The ArgError message strictPatch throws for `patch`, "" when it throws none.
    std::string patchError(const json& patch)
    {
        try
        {
            patched(patch);
        }
        catch (const mcp::ArgError& error)
        {
            return error.what();
        }
        return {};
    }
}

TEST_CASE("settings table kinds: vec2 and vec4 take exactly 2 / 4 numbers inside the bounds and report them back")
{
    const KindsProbe value = patched({{"offset", json::array({1.5, -2})}, {"padding", json::array({0, 100, 50.5, 25})}});
    CHECK(value.offset == glm::vec2(1.5f, -2.0f));
    CHECK(value.padding == glm::vec4(0.0f, 100.0f, 50.5f, 25.0f));

    const json report = env::toJson(probeTable(), value);
    CHECK(report.at("offset") == json::array({1.5, -2.0}));
    CHECK(report.at("padding") == json::array({0.0, 100.0, 50.5, 25.0}));
    // The reported values pass back unchanged.
    const KindsProbe echoed = env::strictPatch(
        probeTable(), value, json{{"offset", report.at("offset")}, {"padding", report.at("padding")}}, "probe");
    CHECK(echoed.offset == value.offset);
    CHECK(echoed.padding == value.padding);

    // The bounds themselves are accepted.
    CHECK(patched({{"offset", json::array({-10, 10})}}).offset == glm::vec2(-10.0f, 10.0f));
    CHECK(patched({{"padding", json::array({100, 0, 100, 0})}}).padding == glm::vec4(100.0f, 0.0f, 100.0f, 0.0f));

    for (const json& bad : {json::array({1}), json::array({1, 2, 3}), json("1, 2"), json::array({1, "2"}),
                            json::array({true, 0}), json::array({11, 0}), json::array({0, -10.5}), json(nullptr)})
    {
        INFO("offset " << bad.dump());
        CHECK_THROWS_AS(patched(json{{"offset", bad}}), mcp::ArgError);
    }
    for (const json& bad : {json::array({0, 0, 0}), json::array({0, 0, 0, 0, 0}), json::array({-1, 0, 0, 0}),
                            json::array({0, 0, 0, 101})})
    {
        INFO("padding " << bad.dump());
        CHECK_THROWS_AS(patched(json{{"padding", bad}}), mcp::ArgError);
    }

    // Errors name the component.
    CHECK(patchError({{"offset", json::array({0, 11})}}).find("'probe.offset[1]'") != std::string::npos);
    CHECK(patchError({{"padding", json::array({0, 0})}}).find("'probe.padding' must be an array of 4 numbers") !=
          std::string::npos);
}

TEST_CASE("settings table kinds: a colour is [r, g, b], [r, g, b, a] or #RRGGBB / #RRGGBBAA and is reported [r, g, b, a]")
{
    CHECK(patched({{"tint", json::array({0.25, 0.5, 1})}}).tint == glm::vec4(0.25f, 0.5f, 1.0f, 1.0f));
    CHECK(patched({{"tint", json::array({0, 0, 0, 0.5})}}).tint == glm::vec4(0.0f, 0.0f, 0.0f, 0.5f));
    CHECK(patched({{"tint", "#FF8000"}}).tint == glm::vec4(1.0f, 128.0f / 255.0f, 0.0f, 1.0f));
    CHECK(patched({{"tint", "#ff800080"}}).tint == glm::vec4(1.0f, 128.0f / 255.0f, 0.0f, 128.0f / 255.0f));
    CHECK(patched({{"tint", "#00fF00"}}).tint == glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));

    // Always four components in the report, and the report passes back unchanged.
    const KindsProbe orange = patched({{"tint", "#FF8000"}});
    const json reported = env::toJson(probeTable(), orange).at("tint");
    REQUIRE(reported.is_array());
    CHECK(reported.size() == 4);
    CHECK(reported.at(3) == 1.0);
    CHECK(env::strictPatch(probeTable(), orange, json{{"tint", reported}}, "probe").tint == orange.tint);

    for (const json& bad : {json("#FFF"), json("FF8000"), json("#GG8000"), json("#FF80001"), json("#FF800"), json("red"),
                            json::array({1, 1}), json::array({1, 1, 1, 1, 1}), json::array({1.5, 0, 0}),
                            json::array({0, 0, 0, -0.1}), json::array({0, "1", 0}), json(1)})
    {
        INFO("tint " << bad.dump());
        CHECK_THROWS_AS(patched(json{{"tint", bad}}), mcp::ArgError);
    }
    CHECK(patchError({{"tint", "#FFF"}}).find("'probe.tint' must be [r, g, b], [r, g, b, a]") != std::string::npos);
    CHECK(patchError({{"tint", json::array({0, 2, 0})}}).find("'probe.tint[1]'") != std::string::npos);

    // Every component, the implied alpha and the hex digits included, must fit the range.
    CHECK(env::requireColor(json::array({0.1, 0.1, 0.1, 0.5}), 0.0, 0.5, "glow") == glm::vec4(0.1f, 0.1f, 0.1f, 0.5f));
    CHECK_THROWS_AS(env::requireColor(json::array({0.1, 0.1, 0.1}), 0.0, 0.5, "glow"), mcp::ArgError);
    CHECK_THROWS_AS(env::requireColor(json("#FFFFFF"), 0.0, 0.5, "glow"), mcp::ArgError);
    CHECK(env::requireColor(json("#0000007F"), 0.0, 0.5, "glow") == glm::vec4(0.0f, 0.0f, 0.0f, 127.0f / 255.0f));
    CHECK_THROWS_AS(env::requireColor(json("#00000080"), 0.0, 0.5, "glow"), mcp::ArgError); // 128/255 > 0.5
}

TEST_CASE("settings table kinds: a string list takes minItems..maxItems strings")
{
    const std::vector<std::string> one{"a"};
    const std::vector<std::string> three{"a", "b", "c"};
    CHECK(patched({{"items", json::array({"a"})}}).items == one);
    CHECK(patched({{"items", json::array({"a", "b", "c"})}}).items == three);
    CHECK(env::toJson(probeTable(), patched({{"items", json::array({"x", "y"})}})).at("items") ==
          json::array({"x", "y"}));

    for (const json& bad : {json::array(), json::array({"a", "b", "c", "d"}), json::array({"a", 1}), json("a"),
                            json::object()})
    {
        INFO("items " << bad.dump());
        CHECK_THROWS_AS(patched(json{{"items", bad}}), mcp::ArgError);
    }
    CHECK(patchError({{"items", json::array()}}).find("'probe.items' must have 1 to 3 items (got 0)") != std::string::npos);
    CHECK(patchError({{"items", json::array({"a", 1})}}).find("'probe.items[1]'") != std::string::npos);
}

TEST_CASE("settings table kinds: the ranges and schema entries of vec2, vec4, colour and string-list keys")
{
    const json ranges = env::rangesJson(probeTable());
    CHECK(ranges.at("offset") == json{{"type", "vec2"}, {"min", -10.0}, {"max", 10.0}});
    CHECK(ranges.at("padding") == json{{"type", "vec4"}, {"min", 0.0}, {"max", 100.0}});
    CHECK(ranges.at("tint") == json{{"type", "color"}, {"min", 0.0}, {"max", 1.0}});
    CHECK(ranges.at("items") == json{{"type", "stringList"}, {"minItems", 1}, {"maxItems", 3}});
    CHECK(ranges.at("count").at("readOnly").get<std::string>() == "set by the engine");
    CHECK_FALSE(ranges.contains("live"));

    const json schema = env::schemaJson(probeTable(), "The probe.");
    CHECK(schema.at("additionalProperties") == false);
    const json& properties = schema.at("properties");

    const json& offset = properties.at("offset");
    CHECK(offset.at("type") == "array");
    CHECK(offset.at("minItems") == 2);
    CHECK(offset.at("maxItems") == 2);
    CHECK(offset.at("items").at("minimum") == -10.0);
    CHECK(offset.at("items").at("maximum") == 10.0);
    CHECK(properties.at("padding").at("minItems") == 4);
    CHECK(properties.at("padding").at("maxItems") == 4);

    const json& tint = properties.at("tint");
    REQUIRE(tint.at("anyOf").size() == 2);
    CHECK(tint.at("anyOf").at(0).at("type") == "array");
    CHECK(tint.at("anyOf").at(0).at("minItems") == 3);
    CHECK(tint.at("anyOf").at(0).at("maxItems") == 4);
    CHECK(tint.at("anyOf").at(1).at("type") == "string");
    CHECK(tint.at("anyOf").at(1).at("pattern") == "^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$");
    CHECK(tint.at("description") == "A tint.");

    const json& items = properties.at("items");
    CHECK(items.at("items").at("type") == "string");
    CHECK(items.at("minItems") == 1);
    CHECK(items.at("maxItems") == 3);

    // Read-only and live-only keys are not settable.
    CHECK_FALSE(properties.contains("count"));
    CHECK_FALSE(properties.contains("live"));
}

TEST_CASE("settings table kinds: dropUnchanged drops the keys the base already holds, out-of-range values included")
{
    KindsProbe base;
    base.speed = 5.0f; // outside 0.5-2: a scene or a script wrote it
    base.count = 7;
    // base.items is empty: fewer than the 1-3 items a patch may set

    const json patch = {
        {"speed", 5},                            // 5 == 5.0
        {"count", 7},                            // read-only, unchanged
        {"offset", json::array({0, 0})},
        {"tint", "#FFFFFF"},                     // the same colour, spelt differently
        {"items", json::array()},
        {"padding", json::array({1, 0, 0, 0})}, // changed
        {"unknown", 1},
        {"live", false}                          // live-only: never reported, so never dropped
    };
    const json remaining = env::dropUnchanged(probeTable(), base, patch, "probe");
    CHECK(remaining == json{{"padding", json::array({1, 0, 0, 0})}, {"unknown", 1}, {"live", false}});
    // strictPatch then reports what was kept.
    CHECK_THROWS_AS(env::strictPatch(probeTable(), base, remaining, "probe"), mcp::ArgError);

    // The echo of an out-of-range value passes with dropUnchanged first and is refused without it.
    const json echo = {{"speed", 5}, {"padding", json::array({1, 0, 0, 0})}};
    const KindsProbe applied =
        env::strictPatch(probeTable(), base, env::dropUnchanged(probeTable(), base, echo, "probe"), "probe");
    CHECK(applied.speed == 5.0f);
    CHECK(applied.padding == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    CHECK_THROWS_AS(env::strictPatch(probeTable(), base, echo, "probe"), mcp::ArgError);

    // A changed value stays for strictPatch to validate: a new read-only value, an out-of-range one, an invalid one.
    CHECK(env::dropUnchanged(probeTable(), base, json{{"count", 8}}, "probe") == json{{"count", 8}});
    CHECK(env::dropUnchanged(probeTable(), base, json{{"speed", 6}}, "probe") == json{{"speed", 6}});
    CHECK(env::dropUnchanged(probeTable(), base, json{{"offset", "0, 0"}}, "probe") == json{{"offset", "0, 0"}});

    // A patch that is not an object comes back as it is (strictPatch refuses it).
    CHECK(env::dropUnchanged(probeTable(), base, json::array({1}), "probe") == json::array({1}));
    CHECK(env::dropUnchanged(probeTable(), base, json::object(), "probe") == json::object());
}
