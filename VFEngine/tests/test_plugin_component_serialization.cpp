#include <doctest.h>

#include <serialization/MetaJsonSerializer.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetGUID.hpp>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <vector>

// ============================================================
// DLL-free round-trip tests for the plugin-component meta <-> JSON
// converters (serialization::meta::*). These mirror exactly what the
// PluginManager serialize/deserialize hooks do at the component level
// (PluginManager.cpp ~408-460): for each reflected member, AssetRef
// fields go through writeAssetRef/readAssetRef, everything else through
// serializeMetaAny / deserializeMetaData. No plugin DLL, no Vulkan/GLFW.
// ============================================================

namespace
{
    // Local enum / nested struct / component registered directly via EnTT meta
    // inside this TU — the same way PluginAPITest.cpp registers its meta types.
    enum class TestElement : int { Fire = 0, Water = 1, Earth = 2, Wind = 3 };

    struct Nested
    {
        std::string label = "none";
        int weight = 0;
        bool flag = false;
    };

    struct PluginTestComponent
    {
        int health = 100;
        float speed = 5.0f;
        bool isActive = true;
        std::string title = "hero";
        glm::vec2 uv{0.0f, 0.0f};
        glm::vec3 offset{0.0f, 1.0f, 0.0f};
        glm::vec4 tint{1.0f, 0.5f, 0.25f, 1.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f}; // (w, x, y, z)
        TestElement element = TestElement::Fire;
        std::vector<int> scores{10, 20, 30};
        std::map<std::string, float> stats{{"strength", 5.0f}, {"agility", 3.5f}};
        Nested nested{"core", 7, true};
        asset::AssetRef iconRef;
    };

    // Register meta once for this TU. EnTT meta is global; a static-init guard
    // keeps it idempotent across the SUBCASE re-entry doctest performs.
    bool registerMeta()
    {
        entt::meta_factory<TestElement>()
            .data<TestElement::Fire>("Fire")
            .data<TestElement::Water>("Water")
            .data<TestElement::Earth>("Earth")
            .data<TestElement::Wind>("Wind");

        entt::meta_factory<Nested>()
            .type("Nested")
            .ctor<>()
            .data<&Nested::label>("label")
            .data<&Nested::weight>("weight")
            .data<&Nested::flag>("flag");

        entt::meta_factory<PluginTestComponent>()
            .type("PluginTestComponent")
            .ctor<>()
            .data<&PluginTestComponent::health>("health")
            .data<&PluginTestComponent::speed>("speed")
            .data<&PluginTestComponent::isActive>("isActive")
            .data<&PluginTestComponent::title>("title")
            .data<&PluginTestComponent::uv>("uv")
            .data<&PluginTestComponent::offset>("offset")
            .data<&PluginTestComponent::tint>("tint")
            .data<&PluginTestComponent::rotation>("rotation")
            .data<&PluginTestComponent::element>("element")
            .data<&PluginTestComponent::scores>("scores")
            .data<&PluginTestComponent::stats>("stats")
            .data<&PluginTestComponent::nested>("nested")
            .data<&PluginTestComponent::iconRef>("iconRef");

        return true;
    }

    void ensureMetaRegistered()
    {
        static const bool once = registerMeta();
        (void)once;
    }

    // Serialize one component instance to JSON, mirroring the PluginManager
    // serialize hook's per-member loop (AssetRef siblings + serializeMetaAny).
    nlohmann::json serializeComponent(const PluginTestComponent& comp)
    {
        ensureMetaRegistered();
        auto metaType = entt::resolve<PluginTestComponent>();
        entt::meta_any any = comp; // copy into a meta_any so member.get(any) works
        nlohmann::json out = nlohmann::json::object();
        for (auto&& [id, member] : metaType.data())
        {
            const char* name = member.name();
            if (!name) continue;
            auto val = member.get(any);
            if (!val) continue;
            if (member.type().info() == entt::type_id<asset::AssetRef>())
            {
                serialization::writeAssetRef(out, name, val.cast<asset::AssetRef>());
                continue;
            }
            auto serialized = serialization::meta::serializeMetaAny(val, member.type());
            if (!serialized.is_null())
                out[name] = std::move(serialized);
        }
        return out;
    }

    // Deserialize JSON back into a component, mirroring the PluginManager
    // deserialize hook's per-member loop. Uses the same owning-meta_any idiom
    // as jsonToMetaAny's class branch (construct, set members, cast back).
    PluginTestComponent deserializeComponent(const nlohmann::json& j)
    {
        ensureMetaRegistered();
        auto metaType = entt::resolve<PluginTestComponent>();
        entt::meta_any instance = metaType.construct();
        REQUIRE(static_cast<bool>(instance));
        for (auto&& [id, member] : metaType.data())
        {
            const char* name = member.name();
            if (!name) continue;
            if (member.type().info() == entt::type_id<asset::AssetRef>())
            {
                member.set(instance, serialization::readAssetRef(j, name, ""));
                continue;
            }
            if (!j.contains(name)) continue;
            serialization::meta::deserializeMetaData(member, instance, j[name]);
        }
        return instance.cast<PluginTestComponent>();
    }
}

TEST_SUITE("PluginComponentSerialization")
{
    TEST_CASE("scalar fields round-trip")
    {
        PluginTestComponent src{};
        src.health = 250;
        src.speed = 12.5f;
        src.isActive = false;
        src.title = "champion";

        auto j = serializeComponent(src);
        auto dst = deserializeComponent(j);

        CHECK(dst.health == 250);
        CHECK(dst.speed == doctest::Approx(12.5f));
        CHECK(dst.isActive == false);
        CHECK(dst.title == "champion");
    }

    TEST_CASE("glm vector fields round-trip")
    {
        PluginTestComponent src{};
        src.uv = glm::vec2(0.3f, -0.7f);
        src.offset = glm::vec3(-1.0f, 2.0f, 3.5f);
        src.tint = glm::vec4(0.1f, 0.2f, 0.3f, 0.4f);

        auto j = serializeComponent(src);
        auto dst = deserializeComponent(j);

        CHECK(dst.uv.x == doctest::Approx(0.3f));
        CHECK(dst.uv.y == doctest::Approx(-0.7f));
        CHECK(dst.offset.x == doctest::Approx(-1.0f));
        CHECK(dst.offset.y == doctest::Approx(2.0f));
        CHECK(dst.offset.z == doctest::Approx(3.5f));
        CHECK(dst.tint.x == doctest::Approx(0.1f));
        CHECK(dst.tint.y == doctest::Approx(0.2f));
        CHECK(dst.tint.z == doctest::Approx(0.3f));
        CHECK(dst.tint.w == doctest::Approx(0.4f));
    }

    TEST_CASE("glm::quat preserves [x,y,z,w] layout")
    {
        // Distinct components so an x/y/z/w transposition would be caught.
        PluginTestComponent src{};
        src.rotation = glm::quat(0.1f, 0.2f, 0.3f, 0.4f); // ctor is (w, x, y, z)

        auto j = serializeComponent(src);

        // Writer emits the array as [x, y, z, w] verbatim.
        REQUIRE(j.contains("rotation"));
        REQUIRE(j["rotation"].is_array());
        REQUIRE(j["rotation"].size() == 4);
        CHECK(j["rotation"][0].get<float>() == doctest::Approx(src.rotation.x));
        CHECK(j["rotation"][1].get<float>() == doctest::Approx(src.rotation.y));
        CHECK(j["rotation"][2].get<float>() == doctest::Approx(src.rotation.z));
        CHECK(j["rotation"][3].get<float>() == doctest::Approx(src.rotation.w));

        auto dst = deserializeComponent(j);
        CHECK(dst.rotation.x == doctest::Approx(src.rotation.x));
        CHECK(dst.rotation.y == doctest::Approx(src.rotation.y));
        CHECK(dst.rotation.z == doctest::Approx(src.rotation.z));
        CHECK(dst.rotation.w == doctest::Approx(src.rotation.w));
    }

    TEST_CASE("enum round-trips by name")
    {
        PluginTestComponent src{};
        src.element = TestElement::Earth;

        auto j = serializeComponent(src);

        // Writer prefers the reflected name.
        REQUIRE(j.contains("element"));
        CHECK(j["element"].is_string());
        CHECK(j["element"].get<std::string>() == "Earth");

        auto dst = deserializeComponent(j);
        CHECK(dst.element == TestElement::Earth);
    }

    TEST_CASE("enum round-trips from an integer value (data-loss bug fix)")
    {
        // The serialize int-fallback path emits a bare integer for enum values
        // without a reflected name; both readers must accept that integer and
        // recover the right member. Hand-build the JSON to exercise it directly.
        ensureMetaRegistered();

        // Start from a fully-serialized component, then overwrite the enum slot
        // with the integer encoding of TestElement::Wind (== 3).
        PluginTestComponent base{};
        auto j = serializeComponent(base);
        j["element"] = 3; // integer, not the "Wind" string

        auto dst = deserializeComponent(j);
        CHECK(dst.element == TestElement::Wind);

        // Also exercise the standalone jsonToMetaAny enum-from-int branch.
        auto enumType = entt::resolve<TestElement>();
        auto any = serialization::meta::jsonToMetaAny(nlohmann::json(2), enumType);
        REQUIRE(static_cast<bool>(any));
        CHECK(any.cast<TestElement>() == TestElement::Earth);
    }

    TEST_CASE("std::vector<int> round-trips")
    {
        PluginTestComponent src{};
        src.scores = {1, 2, 3, 4, 5};

        auto j = serializeComponent(src);
        auto dst = deserializeComponent(j);

        REQUIRE(dst.scores.size() == 5);
        CHECK(dst.scores[0] == 1);
        CHECK(dst.scores[4] == 5);
    }

    TEST_CASE("std::map<std::string,float> round-trips")
    {
        PluginTestComponent src{};
        src.stats = {{"power", 9.0f}, {"defense", 4.25f}};

        auto j = serializeComponent(src);
        auto dst = deserializeComponent(j);

        REQUIRE(dst.stats.size() == 2);
        CHECK(dst.stats.at("power") == doctest::Approx(9.0f));
        CHECK(dst.stats.at("defense") == doctest::Approx(4.25f));
    }

    TEST_CASE("nested struct field round-trips")
    {
        PluginTestComponent src{};
        src.nested = Nested{"engine", 42, true};

        auto j = serializeComponent(src);

        REQUIRE(j.contains("nested"));
        REQUIRE(j["nested"].is_object());

        auto dst = deserializeComponent(j);
        CHECK(dst.nested.label == "engine");
        CHECK(dst.nested.weight == 42);
        CHECK(dst.nested.flag == true);
    }

    TEST_CASE("AssetRef field preserves GUID across round-trip")
    {
        // Fabricate a ref with a known GUID. The test process has a cold
        // AssetDatabase, so resolve() will be empty — assert on the GUID only.
        const uint64_t knownValue = 0x0123456789ABCDEFull;
        asset::AssetRef src = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(knownValue));
        REQUIRE(src.isValid());
        REQUIRE(src.getGUID().getValue() == knownValue);

        PluginTestComponent comp{};
        comp.iconRef = src;

        auto j = serializeComponent(comp);

        // The GUID is written as a 16-hex string under the field key.
        REQUIRE(j.contains("iconRef"));
        CHECK(j["iconRef"].is_string());
        CHECK(j["iconRef"].get<std::string>() == src.getGUID().toString());

        auto dst = deserializeComponent(j);
        CHECK(dst.iconRef.isValid());
        CHECK(dst.iconRef.getGUID() == src.getGUID());
        CHECK(dst.iconRef.getGUID().getValue() == knownValue);
    }

    TEST_CASE("AssetRef invalid (default) round-trips to invalid")
    {
        PluginTestComponent comp{}; // iconRef defaults to invalid
        REQUIRE_FALSE(comp.iconRef.isValid());

        auto j = serializeComponent(comp);
        auto dst = deserializeComponent(j);
        CHECK_FALSE(dst.iconRef.isValid());
    }

    TEST_CASE("wrong JSON value type keeps the field default and does not crash")
    {
        ensureMetaRegistered();

        // Start from a serialized baseline so every other field is well-formed,
        // then corrupt a single field with a type-mismatched value.
        PluginTestComponent base{};
        base.health = 77;
        base.speed = 3.0f;
        auto j = serializeComponent(base);

        // health is an int field; feed it a string. deserializeMetaData should
        // hit the diagnostics else-branch, leave the field at its default, and
        // not throw.
        j["health"] = "not-a-number";
        // speed is a float field; feed it a bool.
        j["speed"] = true;

        PluginTestComponent dst{};
        CHECK_NOTHROW(dst = deserializeComponent(j));

        // Mismatched fields keep the default-constructed values.
        CHECK(dst.health == PluginTestComponent{}.health);
        CHECK(dst.speed == doctest::Approx(PluginTestComponent{}.speed));
        // A well-formed field in the same object still loads correctly.
        CHECK(dst.title == "hero");
    }
}
