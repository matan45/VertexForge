// CPU-only coverage for VK-1657 per-entity VFX parameter overrides:
//   - the shared override JSON codec (vfx/VFXParamOverrideJson.hpp) round-trips every exposed type,
//   - component data is strict: unknown names, wrong types, out-of-range and malformed entries are
//     dropped, and a repeated name collapses in place (last value wins),
//   - SceneSerialization::serializeVFX / deserializeVFX persist the overrides (nothing written when
//     there are none, so older scenes re-save byte-identically),
//   - VFXComponentService get/set carries them and refuses invalid or duplicate ones,
//   - cloneOptionalComponents copies them.

#include <doctest.h>
#include <ostream> // doctest stringifies std::string_view through operator<<

#include <components/Components.hpp>
#include <components/ComponentClone.hpp>
#include <serialization/SceneSerialization.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <vfx/VFXParamOverrideJson.hpp>

#include <impl/components/VFXComponentService.hpp>
#include <data/EntityConversion.hpp>

#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <vector>

namespace
{
    // Exactly representable values, so a JSON round trip compares equal.
    vfx::VFXPropertyValue sampleValue(const vfx::VFXExposedParameter& parameter)
    {
        switch (parameter.target)
        {
        case vfx::VFXOverrideTarget::BlendMode:  return int32_t{3};
        case vfx::VFXOverrideTarget::SortOrder:  return int32_t{-12};
        case vfx::VFXOverrideTarget::RenderMode: return int32_t{2};
        default: break;
        }
        switch (parameter.type)
        {
        case vfx::VFXPropertyType::Float: return 1.5f;
        case vfx::VFXPropertyType::Bool:  return true;
        case vfx::VFXPropertyType::Vec3:  return glm::vec3(1.0f, 2.0f, 3.0f);
        case vfx::VFXPropertyType::Color: return glm::vec4(0.5f, 0.25f, 1.0f, 1.0f);
        default:                          return vfx::defaultValueFor(parameter.type);
        }
    }

    services::EntityHandle handleOf(const scene::Entity& e)
    {
        return services::internal::toHandle(e.getHandle());
    }
}

TEST_SUITE("VFXComponentOverrides")
{
    TEST_CASE("codec round-trips a value of every exposed parameter")
    {
        for (const auto& parameter : vfx::kExposedParameters)
        {
            const std::string name(parameter.name);
            CAPTURE(name);
            const vfx::VFXParamOverride original{name, sampleValue(parameter)};
            REQUIRE(vfx::isValidOverride(original));

            const nlohmann::json j = vfx::paramOverrideToJson(original);
            REQUIRE(j.is_array());
            REQUIRE(j.size() == 3);
            CHECK(j[0].get<std::string>() == name);
            CHECK(j[1].get<std::string>() == vfx::propertyTypeToString(parameter.type));

            const auto parsed = vfx::paramOverrideFromJson(j);
            REQUIRE(parsed.has_value());
            CHECK(parsed->name == original.name);
            CHECK(parsed->value.index() == original.value.index());
            CHECK(vfx::paramOverrideToJson(*parsed) == j);

            const auto component = vfx::componentOverridesFromJson(nlohmann::json::array({j}));
            REQUIRE(component.size() == 1);
            CHECK(vfx::sameOverrides(component, {original}));
        }
    }

    TEST_CASE("component overrides drop unknown, mistyped, out-of-range and malformed entries")
    {
        const nlohmann::json array = nlohmann::json::array({
            nlohmann::json::array({"startSize", "Float", 1.6}),
            nlohmann::json::array({"notAParameter", "Float", 1.0}),
            nlohmann::json::array({"spawnRate", "Float", "fast"}),
            nlohmann::json::array({"blendMode", "Int", 7}),
            nlohmann::json::array({"colorTint", "Color", nlohmann::json::array({1.0, 0.5})}),
            nlohmann::json::array({"colorTint", "Color", nlohmann::json::array({-1.0, 1.0, 1.0, 1.0})}),
            nlohmann::json::array({"lifetime", "Int", 2}),
            42,
            nlohmann::json::array({"sortOrder", "Int", 4, "extra"})
        });

        std::vector<std::string> rejected;
        const auto overrides = vfx::componentOverridesFromJson(array, &rejected);

        REQUIRE(overrides.size() == 1);
        CHECK(overrides[0].name == "startSize");
        REQUIRE(std::holds_alternative<float>(overrides[0].value));
        CHECK(std::get<float>(overrides[0].value) == doctest::Approx(1.6f));

        const std::vector<std::string> expected{
            "notAParameter", "spawnRate", "blendMode", "colorTint", "colorTint", "lifetime",
            "<malformed>", "<malformed>"};
        CHECK(rejected == expected);

        CHECK(vfx::componentOverridesFromJson(nlohmann::json::object()).empty());
        CHECK(vfx::componentOverridesFromJson(nlohmann::json()).empty());

        // An integer outside int32 is refused, not truncated into range (4294967298 would read as 2).
        std::vector<std::string> wide;
        CHECK(vfx::componentOverridesFromJson(
                  nlohmann::json::array({nlohmann::json::array({"sortOrder", "Int", 4294967298LL})}), &wide).empty());
        CHECK(wide == std::vector<std::string>{"sortOrder"});
    }

    TEST_CASE("a repeated name collapses in place and the last value wins")
    {
        const nlohmann::json array = nlohmann::json::array({
            nlohmann::json::array({"startSize", "Float", 1.0}),
            nlohmann::json::array({"sortOrder", "Int", 5}),
            nlohmann::json::array({"startSize", "Float", 2.0})
        });

        const auto overrides = vfx::componentOverridesFromJson(array);
        REQUIRE(overrides.size() == 2);
        CHECK(overrides[0].name == "startSize");
        CHECK(std::get<float>(overrides[0].value) == doctest::Approx(2.0f));
        CHECK(overrides[1].name == "sortOrder");
        CHECK(std::get<int32_t>(overrides[1].value) == 5);
    }

    TEST_CASE("sameOverrides compares names, order and values")
    {
        const std::vector<vfx::VFXParamOverride> a{{"startSize", 1.0f}, {"sortOrder", int32_t{5}}};
        const std::vector<vfx::VFXParamOverride> same{{"startSize", 1.0f}, {"sortOrder", int32_t{5}}};
        const std::vector<vfx::VFXParamOverride> reordered{{"sortOrder", int32_t{5}}, {"startSize", 1.0f}};
        const std::vector<vfx::VFXParamOverride> changed{{"startSize", 1.5f}, {"sortOrder", int32_t{5}}};
        const std::vector<vfx::VFXParamOverride> shorter{{"startSize", 1.0f}};

        CHECK(vfx::sameOverrides(a, same));
        CHECK(vfx::sameOverrides({}, {}));
        CHECK_FALSE(vfx::sameOverrides(a, reordered));
        CHECK_FALSE(vfx::sameOverrides(a, changed));
        CHECK_FALSE(vfx::sameOverrides(a, shorter));
    }

    TEST_CASE("serializeVFX writes overrides only when present and deserializeVFX restores them")
    {
        components::VFXComponent vfxComp;
        vfxComp.autoPlay = false;

        const nlohmann::json withoutOverrides = serialization::SceneSerialization::serializeVFX(vfxComp);
        CHECK_FALSE(withoutOverrides.contains("overrides"));

        vfxComp.overrides = {
            {"startSize", 1.5f},
            {"colorTint", glm::vec4(1.0f, 0.5f, 0.25f, 1.0f)},
            {"blendMode", int32_t{1}}
        };
        const nlohmann::json j = serialization::SceneSerialization::serializeVFX(vfxComp);
        REQUIRE(j.contains("overrides"));
        CHECK(j["overrides"].size() == 3);

        components::VFXComponent loaded;
        loaded.runtimeInstanceId = 7;
        loaded.isPlaying = true;
        serialization::SceneSerialization::deserializeVFX(j, loaded);
        CHECK(vfx::sameOverrides(loaded.overrides, vfxComp.overrides));
        CHECK(loaded.autoPlay == false);
        CHECK(loaded.runtimeInstanceId == 0u);
        CHECK_FALSE(loaded.isPlaying);

        // A component saved without the key loads without overrides, even into a reused struct.
        components::VFXComponent reused;
        reused.overrides = vfxComp.overrides;
        serialization::SceneSerialization::deserializeVFX(withoutOverrides, reused);
        CHECK(reused.overrides.empty());
    }

    TEST_CASE("deserializeVFX drops invalid stored overrides and keeps the valid ones")
    {
        nlohmann::json j = serialization::SceneSerialization::serializeVFX(components::VFXComponent{});
        j["overrides"] = nlohmann::json::array({
            nlohmann::json::array({"blendMode", "Int", 9}),
            nlohmann::json::array({"startSize", "Float", 2.0})
        });

        components::VFXComponent loaded;
        serialization::SceneSerialization::deserializeVFX(j, loaded);
        REQUIRE(loaded.overrides.size() == 1);
        CHECK(loaded.overrides[0].name == "startSize");
    }

    TEST_CASE("VFXComponentService get/set carries overrides and refuses invalid or duplicate ones")
    {
        services::VFXComponentService service(std::make_shared<scene::SceneGraphSystem>());
        scene::Entity entity("VFXOverrideEntity");
        entity.addComponent<components::VFXComponent>();
        const services::EntityHandle handle = handleOf(entity);

        auto data = service.getVFXData(handle);
        REQUIRE(data.has_value());
        CHECK(data->overrides.empty());

        data->overrides = {{"startSize", 1.6f}, {"colorTint", glm::vec4(1.0f, 0.6f, 0.25f, 1.0f)}};
        CHECK(service.setVFXData(handle, *data));
        const auto written = service.getVFXData(handle);
        REQUIRE(written.has_value());
        CHECK(vfx::sameOverrides(written->overrides, data->overrides));
        CHECK(vfx::sameOverrides(entity.getComponent<components::VFXComponent>().overrides, data->overrides));

        auto outOfRange = *written;
        outOfRange.loop = false;
        outOfRange.overrides.push_back({"blendMode", int32_t{9}});
        CHECK_FALSE(service.setVFXData(handle, outOfRange));

        auto duplicate = *written;
        duplicate.loop = false;
        duplicate.overrides.push_back({"startSize", 2.0f});
        CHECK_FALSE(service.setVFXData(handle, duplicate));

        auto unknown = *written;
        unknown.loop = false;
        unknown.overrides.push_back({"notAParameter", 1.0f});
        CHECK_FALSE(service.setVFXData(handle, unknown));

        // The refused writes left the component untouched.
        const auto after = service.getVFXData(handle);
        REQUIRE(after.has_value());
        CHECK(after->loop == true);
        CHECK(vfx::sameOverrides(after->overrides, data->overrides));

        scene::EntityRegistry::getRegistry().destroy(entity.getHandle());
    }

    TEST_CASE("cloneOptionalComponents copies the overrides")
    {
        scene::Entity src("VFXOverrideSource");
        scene::Entity dst("VFXOverrideDest");

        auto& v = src.addComponent<components::VFXComponent>();
        v.overrides = {{"spawnRate", 80.0f}, {"sortOrder", int32_t{3}}};

        components::cloneOptionalComponents(src, dst);

        REQUIRE(dst.hasComponent<components::VFXComponent>());
        CHECK(vfx::sameOverrides(dst.getComponent<components::VFXComponent>().overrides, v.overrides));

        scene::EntityRegistry::getRegistry().destroy(src.getHandle());
        scene::EntityRegistry::getRegistry().destroy(dst.getHandle());
    }
}
