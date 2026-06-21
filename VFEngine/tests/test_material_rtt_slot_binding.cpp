// CPU-only coverage for VK-1418: per-entity binding of a material texture slot
// (albedo/emission) to a live Render Texture.
//
// Mirrors the BillboardComponent renderTextureSource by-name re-resolve pattern:
//   - Only the source *name* is serialized; the entity handle is runtime-only.
//   - On scene load, deserializeMaterial clears the handle and stores the name;
//     resolveRenderTextureSourceNames re-binds the handle from the name.
//   - The MaterialComponentService get/set DTO conversion round-trips the binding
//     and resolves the source entity from its name on set.
//
// All CPU-only: no Vulkan device, no GLFW window. Tests link ECSRegistry so the
// EntityRegistry singleton resolves to one instance across the process.

#include <doctest.h>

#include <components/Components.hpp>
#include <serialization/SceneSerialization.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <scene/SceneGraphSystem.hpp>

#include <impl/components/MaterialComponentService.hpp>
#include <data/EntityConversion.hpp>

#include <nlohmann/json.hpp>
#include <memory>

namespace
{
    scene::Entity makeEntity()
    {
        return scene::Entity(scene::EntityRegistry::getRegistry().create());
    }

    services::EntityHandle handleOf(const scene::Entity& e)
    {
        return services::internal::toHandle(e.getHandle());
    }
}

TEST_SUITE("MaterialRttSlotBinding")
{
    TEST_CASE("serialize/deserialize round-trips slot bindings by name, resetting the handle")
    {
        components::MaterialComponent mat;
        // A stray runtime handle must NOT be serialized; only the name persists.
        mat.renderTextureSlotBindings["albedo"] = {"MonitorFeed", static_cast<entt::entity>(123)};
        mat.renderTextureSlotBindings["emission"] = {"GlowFeed", static_cast<entt::entity>(456)};

        nlohmann::json j = serialization::SceneSerialization::serializeMaterial(mat);

        REQUIRE(j.contains("renderTextureSlotBindings"));
        CHECK(j["renderTextureSlotBindings"]["albedo"].get<std::string>() == "MonitorFeed");
        CHECK(j["renderTextureSlotBindings"]["emission"].get<std::string>() == "GlowFeed");

        components::MaterialComponent loaded;
        serialization::SceneSerialization::deserializeMaterial(j, loaded);

        REQUIRE(loaded.renderTextureSlotBindings.count("albedo") == 1);
        REQUIRE(loaded.renderTextureSlotBindings.count("emission") == 1);
        CHECK(loaded.renderTextureSlotBindings["albedo"].sourceName == "MonitorFeed");
        CHECK(loaded.renderTextureSlotBindings["emission"].sourceName == "GlowFeed");
        // The handle is runtime-only — it must come back as null, resolved later by name.
        // Extra parens force eager bool eval, avoiding the doctest/EnTT operator== ambiguity.
        CHECK((loaded.renderTextureSlotBindings["albedo"].source == entt::null));
        CHECK((loaded.renderTextureSlotBindings["emission"].source == entt::null));
    }

    TEST_CASE("empty-name bindings are not serialized")
    {
        components::MaterialComponent mat;
        mat.renderTextureSlotBindings["albedo"] = {"", entt::null};

        nlohmann::json j = serialization::SceneSerialization::serializeMaterial(mat);
        CHECK_FALSE(j.contains("renderTextureSlotBindings"));
    }

    TEST_CASE("resolveRenderTextureSourceNames binds the slot source from its name")
    {
        // RTT-bearing, named source entity.
        scene::Entity rttEntity = makeEntity();
        rttEntity.addComponent<components::RenderTextureComponent>();
        rttEntity.addComponent<components::NameComponent>().name = "SecurityCam";

        // Consumer entity with a material slot bound to that name, handle unresolved.
        scene::Entity meshEntity = makeEntity();
        auto& mat = meshEntity.addComponent<components::MaterialComponent>();
        mat.renderTextureSlotBindings["albedo"] = {"SecurityCam", entt::null};

        serialization::SceneSerialization::resolveRenderTextureSourceNames();

        const auto& after = meshEntity.getComponent<components::MaterialComponent>();
        CHECK(after.renderTextureSlotBindings.at("albedo").source == rttEntity.getHandle());
    }

    TEST_CASE("MaterialComponentService get/set round-trips and resolves the binding by name")
    {
        scene::Entity rttEntity = makeEntity();
        rttEntity.addComponent<components::RenderTextureComponent>();
        rttEntity.addComponent<components::NameComponent>().name = "Feed01";

        scene::Entity meshEntity = makeEntity();
        meshEntity.addComponent<components::MaterialComponent>();

        services::MaterialComponentService service(std::make_shared<scene::SceneGraphSystem>());

        // Editor commits a binding through the DTO (name carries the identity).
        services::MaterialData data;
        services::RenderTextureSlotBindingData binding;
        binding.sourceName = "Feed01";
        binding.source = services::EntityHandle::invalid(); // editor may not know the handle
        data.renderTextureSlotBindings["emission"] = binding;

        REQUIRE(service.setMaterialData(handleOf(meshEntity), data));

        // The component must have resolved the source entity from the name.
        const auto& comp = meshEntity.getComponent<components::MaterialComponent>();
        REQUIRE(comp.renderTextureSlotBindings.count("emission") == 1);
        CHECK(comp.renderTextureSlotBindings.at("emission").sourceName == "Feed01");
        CHECK(comp.renderTextureSlotBindings.at("emission").source == rttEntity.getHandle());

        // get surfaces the binding back out with the resolved handle.
        auto out = service.getMaterialData(handleOf(meshEntity));
        REQUIRE(out.has_value());
        REQUIRE(out->renderTextureSlotBindings.count("emission") == 1);
        CHECK(out->renderTextureSlotBindings.at("emission").sourceName == "Feed01");
        CHECK(out->renderTextureSlotBindings.at("emission").source.isValid());
    }

    TEST_CASE("setMaterialData drops bindings with empty source names")
    {
        scene::Entity meshEntity = makeEntity();
        meshEntity.addComponent<components::MaterialComponent>();

        services::MaterialComponentService service(std::make_shared<scene::SceneGraphSystem>());

        services::MaterialData data;
        services::RenderTextureSlotBindingData binding; // empty sourceName
        data.renderTextureSlotBindings["albedo"] = binding;

        REQUIRE(service.setMaterialData(handleOf(meshEntity), data));

        const auto& comp = meshEntity.getComponent<components::MaterialComponent>();
        CHECK(comp.renderTextureSlotBindings.empty());
    }
}
