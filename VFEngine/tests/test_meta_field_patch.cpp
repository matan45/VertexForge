#include <doctest.h>

#include <serialization/MetaFieldPatch.hpp>
#include <asset/AssetRef.hpp>
#include <asset/AssetGUID.hpp>

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <map>
#include <string>
#include <string_view>
#include <vector>

// ============================================================
// VK-1651: strict by-name patching of EnTT-meta reflected (plugin) components
// (serialization::meta::applyFields / serializeFields). CPU-only; the meta types
// are registered locally the same way a plugin's registerNativeComponent does.
// ============================================================

namespace
{
    enum class PatchElement : int { Fire = 0, Water = 1, Earth = 2 };

    struct PatchInner
    {
        std::string label = "inner";
        int weight = 3;
        bool flag = false;
    };

    struct PatchComponent
    {
        int health = 100;
        float speed = 5.0f;
        bool isActive = true;
        std::string title = "hero";
        glm::vec3 offset{0.0f, 1.0f, 0.0f};
        PatchElement element = PatchElement::Fire;
        std::vector<int> scores{10, 20, 30};
        std::map<std::string, float> stats{{"strength", 5.0f}};
        PatchInner inner{};
        asset::AssetRef iconRef;
    };

    bool registerMeta()
    {
        entt::meta_factory<PatchElement>()
            .type("PatchElement")
            .data<PatchElement::Fire>("Fire")
            .data<PatchElement::Water>("Water")
            .data<PatchElement::Earth>("Earth");

        entt::meta_factory<PatchInner>()
            .type("PatchInner")
            .ctor<>()
            .data<&PatchInner::label>("label")
            .data<&PatchInner::weight>("weight")
            .data<&PatchInner::flag>("flag");

        entt::meta_factory<PatchComponent>()
            .type("PatchComponent")
            .ctor<>()
            .data<&PatchComponent::health>("health")
            .data<&PatchComponent::speed>("speed")
            .data<&PatchComponent::isActive>("isActive")
            .data<&PatchComponent::title>("title")
            .data<&PatchComponent::offset>("offset")
            .data<&PatchComponent::element>("element")
            .data<&PatchComponent::scores>("scores")
            .data<&PatchComponent::stats>("stats")
            .data<&PatchComponent::inner>("inner")
            .data<&PatchComponent::iconRef>("iconRef");

        return true;
    }

    entt::meta_type patchType()
    {
        static const bool once = registerMeta();
        (void)once;
        return entt::resolve<PatchComponent>();
    }

    // Patch `comp` in place through a reference meta_any, exactly as the editor
    // handler does with bridge.metaType.from_void(ptr).
    serialization::meta::PatchResult patch(PatchComponent& comp, const nlohmann::json& fields,
                                           const std::function<bool(std::string_view)>& isReadOnly = {})
    {
        auto type = patchType();
        auto ref = type.from_void(&comp);
        REQUIRE(static_cast<bool>(ref));
        return serialization::meta::applyFields(ref, type, fields, isReadOnly);
    }

    bool isUntouched(const PatchComponent& comp)
    {
        const PatchComponent defaults{};
        return comp.health == defaults.health && comp.speed == defaults.speed
            && comp.isActive == defaults.isActive && comp.title == defaults.title
            && comp.offset == defaults.offset && comp.element == defaults.element
            && comp.scores == defaults.scores && comp.stats == defaults.stats
            && comp.inner.label == defaults.inner.label && comp.inner.weight == defaults.inner.weight
            && comp.inner.flag == defaults.inner.flag && !comp.iconRef.isValid();
    }
}

TEST_SUITE("MetaFieldPatch")
{
    TEST_CASE("sets scalar, vec3 and enum fields by name")
    {
        PatchComponent comp{};
        auto result = patch(comp, {
            {"health", 42}, {"speed", 2.5}, {"isActive", false}, {"title", "scout"},
            {"offset", {1.0, -2.0, 3.5}}, {"element", "Water"}
        });

        REQUIRE(result.ok);
        CHECK(result.error.empty());
        CHECK(comp.health == 42);
        CHECK(comp.speed == doctest::Approx(2.5f));
        CHECK(comp.isActive == false);
        CHECK(comp.title == "scout");
        CHECK(comp.offset.x == doctest::Approx(1.0f));
        CHECK(comp.offset.y == doctest::Approx(-2.0f));
        CHECK(comp.offset.z == doctest::Approx(3.5f));
        CHECK(comp.element == PatchElement::Water);
    }

    TEST_CASE("enum accepts its underlying integer and rejects unknown names")
    {
        PatchComponent comp{};
        REQUIRE(patch(comp, {{"element", 2}}).ok);
        CHECK(comp.element == PatchElement::Earth);

        auto bad = patch(comp, {{"element", "Lightning"}});
        CHECK_FALSE(bad.ok);
        CHECK(bad.error.find("element") != std::string::npos);
        CHECK(bad.error.find("Water") != std::string::npos);
        CHECK(comp.element == PatchElement::Earth);
    }

    TEST_CASE("unknown field fails, lists valid names and changes nothing")
    {
        PatchComponent comp{};
        auto result = patch(comp, {{"health", 1}, {"mana", 50}});

        CHECK_FALSE(result.ok);
        CHECK(result.error.find("mana") != std::string::npos);
        CHECK(result.error.find("health") != std::string::npos);
        CHECK(result.error.find("speed") != std::string::npos);
        CHECK(result.error.find("iconRefPath") != std::string::npos);
        CHECK(isUntouched(comp));
    }

    TEST_CASE("one bad field among good ones changes no field")
    {
        SUBCASE("wrong JSON type for a scalar")
        {
            PatchComponent comp{};
            auto result = patch(comp, {{"health", 7}, {"title", "x"}, {"speed", "fast"}});
            CHECK_FALSE(result.ok);
            CHECK(result.error.find("speed") != std::string::npos);
            CHECK(result.error.find("float") != std::string::npos);
            CHECK(isUntouched(comp));
        }
        SUBCASE("non-numeric vec3 element (jsonToMetaAny throws)")
        {
            PatchComponent comp{};
            auto result = patch(comp, {{"health", 7}, {"offset", {1.0, "x", 3.0}}});
            CHECK_FALSE(result.ok);
            CHECK(result.error.find("offset") != std::string::npos);
            CHECK(isUntouched(comp));
        }
        SUBCASE("vec3 with the wrong arity")
        {
            PatchComponent comp{};
            auto result = patch(comp, {{"health", 7}, {"offset", {1.0, 2.0, 3.0, 4.0}}});
            CHECK_FALSE(result.ok);
            CHECK(result.error.find("vec3") != std::string::npos);
            CHECK(isUntouched(comp));
        }
        SUBCASE("bad element inside a vector")
        {
            PatchComponent comp{};
            auto result = patch(comp, {{"health", 7}, {"scores", {1, "two", 3}}});
            CHECK_FALSE(result.ok);
            CHECK(result.error.find("scores[1]") != std::string::npos);
            CHECK(isUntouched(comp));
        }
        SUBCASE("bad nested sub-field")
        {
            PatchComponent comp{};
            auto result = patch(comp, {{"health", 7}, {"inner", {{"weight", 1}, {"bogus", true}}}});
            CHECK_FALSE(result.ok);
            CHECK(result.error.find("bogus") != std::string::npos);
            CHECK(isUntouched(comp));
        }
    }

    TEST_CASE("fields must be a JSON object")
    {
        PatchComponent comp{};
        auto result = patch(comp, nlohmann::json::array({1, 2}));
        CHECK_FALSE(result.ok);
        CHECK(isUntouched(comp));
    }

    TEST_CASE("read-only predicate rejects the whole patch")
    {
        PatchComponent comp{};
        auto result = patch(comp, {{"health", 1}, {"speed", 9.0}},
                            [](std::string_view field) { return field == "speed"; });
        CHECK_FALSE(result.ok);
        CHECK(result.error.find("speed") != std::string::npos);
        CHECK(result.error.find("read-only") != std::string::npos);
        CHECK(isUntouched(comp));

        // Fields the predicate allows still apply.
        REQUIRE(patch(comp, {{"health", 1}}, [](std::string_view field) { return field == "speed"; }).ok);
        CHECK(comp.health == 1);
    }

    TEST_CASE("nested struct patch merges and keeps untouched sub-fields")
    {
        PatchComponent comp{};
        comp.inner = PatchInner{"kept", 9, true};

        REQUIRE(patch(comp, {{"inner", {{"weight", 4}}}}).ok);
        CHECK(comp.inner.weight == 4);
        CHECK(comp.inner.label == "kept");
        CHECK(comp.inner.flag == true);
    }

    TEST_CASE("containers are replaced wholesale")
    {
        PatchComponent comp{};
        REQUIRE(patch(comp, {{"scores", {7, 8}}, {"stats", {{"agility", 1.5}}}}).ok);
        CHECK(comp.scores == std::vector<int>{7, 8});
        REQUIRE(comp.stats.size() == 1);
        CHECK(comp.stats.at("agility") == doctest::Approx(1.5f));

        REQUIRE(patch(comp, {{"scores", nlohmann::json::array()}}).ok);
        CHECK(comp.scores.empty());

        CHECK_FALSE(patch(comp, {{"stats", {{"agility", "high"}}}}).ok);
        CHECK(comp.stats.at("agility") == doctest::Approx(1.5f));
    }

    TEST_CASE("AssetRef field accepts GUID hex and null, rejects garbage")
    {
        const uint64_t knownValue = 0x0123456789ABCDEFull;
        const auto guid = asset::AssetGUID::fromValue(knownValue);

        PatchComponent comp{};
        REQUIRE(patch(comp, {{"iconRef", guid.toString()}}).ok);
        CHECK(comp.iconRef.isValid());
        CHECK(comp.iconRef.getGUID().getValue() == knownValue);

        auto bad = patch(comp, {{"iconRef", "nothex"}});
        CHECK_FALSE(bad.ok);
        CHECK(comp.iconRef.getGUID().getValue() == knownValue);

        REQUIRE(patch(comp, {{"iconRef", nullptr}}).ok);
        CHECK_FALSE(comp.iconRef.isValid());
    }

    TEST_CASE("serialized unset AssetRef patches back (undo restores an empty ref)")
    {
        // writeAssetRef emits the all-zero GUID for an unset ref; Set must accept it.
        PatchComponent empty{};
        auto type = patchType();
        auto emptyRef = type.from_void(&empty);
        const auto snapshot = serialization::meta::serializeFields(emptyRef, type);
        REQUIRE(snapshot.contains("iconRef"));

        PatchComponent comp{};
        comp.iconRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0x99ull));
        auto result = patch(comp, {{"iconRef", snapshot.at("iconRef")}});
        REQUIRE_MESSAGE(result.ok, result.error);
        CHECK_FALSE(comp.iconRef.isValid());
    }

    TEST_CASE("patch through from_void reaches the live component in a registry")
    {
        entt::registry registry;
        const auto entity = registry.create();
        registry.emplace<PatchComponent>(entity);

        auto type = patchType();
        auto ref = type.from_void(registry.try_get<PatchComponent>(entity));
        REQUIRE(static_cast<bool>(ref));
        REQUIRE(serialization::meta::applyFields(ref, type, {{"health", 3}, {"inner", {{"label", "live"}}}}).ok);

        const auto& live = registry.get<PatchComponent>(entity);
        CHECK(live.health == 3);
        CHECK(live.inner.label == "live");
        CHECK(live.inner.weight == PatchInner{}.weight);
    }

    TEST_CASE("serializeFields output round-trips through applyFields")
    {
        PatchComponent src{};
        src.health = 11;
        src.speed = 0.5f;
        src.title = "copy";
        src.offset = glm::vec3(4.0f, 5.0f, 6.0f);
        src.element = PatchElement::Earth;
        src.scores = {1, 2};
        src.stats = {{"wisdom", 2.0f}};
        src.inner = PatchInner{"deep", 8, true};
        src.iconRef = asset::AssetRef::fromGUID(asset::AssetGUID::fromValue(0x42ull));

        auto type = patchType();
        auto srcRef = type.from_void(&src);
        const auto json = serialization::meta::serializeFields(srcRef, type);
        CHECK(json.at("element") == "Earth");
        CHECK(json.at("iconRef") == src.iconRef.toHexString());

        PatchComponent dst{};
        auto result = patch(dst, json);
        REQUIRE_MESSAGE(result.ok, result.error);
        CHECK(dst.health == 11);
        CHECK(dst.speed == doctest::Approx(0.5f));
        CHECK(dst.title == "copy");
        CHECK(dst.offset == src.offset);
        CHECK(dst.element == PatchElement::Earth);
        CHECK(dst.scores == src.scores);
        CHECK(dst.stats == src.stats);
        CHECK(dst.inner.label == "deep");
        CHECK(dst.inner.weight == 8);
        CHECK(dst.inner.flag == true);
        CHECK(dst.iconRef == src.iconRef);

        auto dstRef = type.from_void(&dst);
        CHECK(serialization::meta::serializeFields(dstRef, type) == json);
    }

    TEST_CASE("metaTypeName describes reflected field types")
    {
        auto type = patchType();
        std::map<std::string, std::string> names;
        for (auto&& [id, member] : type.data())
            names[std::string(member.name())] = serialization::meta::metaTypeName(member.type());

        CHECK(names["health"] == "int");
        CHECK(names["speed"] == "float");
        CHECK(names["title"] == "string");
        CHECK(names["offset"] == "vec3");
        CHECK(names["element"] == "enum:PatchElement");
        CHECK(names["scores"] == "array<int>");
        CHECK(names["stats"] == "map<string,float>");
        CHECK(names["inner"] == "struct:PatchInner");
        CHECK(names["iconRef"] == "AssetRef");
    }
}
