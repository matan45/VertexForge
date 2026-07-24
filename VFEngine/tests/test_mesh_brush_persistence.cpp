// VK-1570 — mesh-brush persistent instance tracking.
//   1) MeshBrushInstanceComponent round-trips through scene save/load (serializer).
//   2) SceneLoadedNotification rebuilds the brush's spatial grid at WORLD positions
//      reconstructed from parent(sector center)+instance(local offset) — the C1 fix.
//      A naive GetWorldTransformQuery rebuild would place instances at their local
//      (sector-relative) offset; this test fails loudly if that regresses.
//   3) "Erase selected type only" removes just the selected palette entry's instances.
//
// CPU-only: no Vulkan device / window. Entities live on the process-wide singleton
// EntityRegistry. Each test clears the dispatcher at both ends and destroys the
// entities it created so the shared registry/dispatcher don't leak across tests.

#include <doctest.h>

#include <impl/meshbrush/MeshBrushServiceImpl.hpp>
#include <serialization/SceneSerialization.hpp>
#include <scene/SceneGraphSystem.hpp>
#include <scene/Entity.hpp>
#include <scene/EntityRegistry.hpp>
#include <components/Components.hpp>
#include <data/EntityConversion.hpp>
#include <data/DTOs.hpp>
#include <asset/AssetDatabase.hpp>
#include <events/EventDispatcher.hpp>
#include <events/meshbrush/MeshBrushEvents.hpp>
#include <events/scene/EntityTransformEvents.hpp>
#include <events/scene/ScenePersistenceEvents.hpp>
#include <events/scene/ComponentMediaEvents.hpp>
#include <events/render/MaterialEvents.hpp>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <vector>

namespace
{
    namespace fs = std::filesystem;

    // A minimal stand-in for the transform/hierarchy/media services the rebuild queries.
    // Holds the reloaded scene: real registry entities carrying MeshBrushInstanceComponent,
    // their LOCAL transforms, and their parent handles — exactly what exists post-load.
    struct FakeScene
    {
        entt::registry& registry = scene::EntityRegistry::getRegistry();
        std::vector<entt::entity> created;
        std::map<uint64_t, services::TransformData> localTransforms;         // handle.id -> local
        std::map<uint64_t, std::optional<services::EntityHandle>> parents;    // handle.id -> parent
        std::vector<services::EntityHandle> deleted;                         // captured DeleteEntityCommand

        // A pure-translation "MeshBrush_*" group entity at a sector center (no brush component).
        services::EntityHandle addGroup(const glm::vec3& worldCenter)
        {
            entt::entity e = registry.create();
            created.push_back(e);
            auto h = services::internal::toHandle(e);
            services::TransformData t;
            t.position = worldCenter;
            localTransforms[h.id] = t;
            parents[h.id] = std::nullopt;
            return h;
        }

        // A painted instance: carries the component (so the rebuild's view walk sees it) and a
        // sector-relative LOCAL transform, parented to a group.
        services::EntityHandle addInstance(uint32_t paletteIndex, const glm::vec3& localPos,
                                           services::EntityHandle parent)
        {
            entt::entity e = registry.create();
            created.push_back(e);
            auto& comp = registry.emplace<components::MeshBrushInstanceComponent>(e);
            comp.paletteIndex = paletteIndex;
            comp.surfaceNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            auto h = services::internal::toHandle(e);
            services::TransformData t;
            t.position = localPos;
            localTransforms[h.id] = t;
            parents[h.id] = parent;
            return h;
        }

        ~FakeScene()
        {
            for (entt::entity e : created)
                if (registry.valid(e))
                    registry.destroy(e);
        }
    };

    void installSceneQueryFakes(FakeScene& scene)
    {
        auto& d = events::EventDispatcher::instance();

        d.registerQueryHandler<events::scene::GetTransformQuery>(
            [&scene](const events::scene::GetTransformQuery& q) -> std::optional<services::TransformData>
            {
                auto it = scene.localTransforms.find(q.entity.id);
                if (it == scene.localTransforms.end()) return std::nullopt;
                return it->second;
            });

        d.registerQueryHandler<events::scene::GetEntityQuery>(
            [&scene](const events::scene::GetEntityQuery& q) -> std::optional<services::EntityData>
            {
                auto it = scene.parents.find(q.entity.id);
                if (it == scene.parents.end()) return std::nullopt;
                services::EntityData data;
                data.handle = q.entity;
                data.parent = it->second;
                return data;
            });

        // No mesh/material recovery needed for these tests — erase keys on the entity handle.
        d.registerQueryHandler<events::scene::GetMeshDataQuery>(
            [](const events::scene::GetMeshDataQuery&) -> std::optional<services::MeshData>
            { return std::nullopt; });
        d.registerQueryHandler<events::material::GetMaterialDataQuery>(
            [](const events::material::GetMaterialDataQuery&) -> std::optional<services::MaterialData>
            { return std::nullopt; });

        d.registerCommandHandler<events::scene::DeleteEntityCommand>(
            [&scene](const events::scene::DeleteEntityCommand& cmd)
            {
                scene.deleted.push_back(cmd.entity);
                return true;
            });
    }

    // Activate the brush in Erase mode with a non-empty palette (applyBrush early-outs on an
    // empty palette) and the given radius/selected-type settings.
    void activateErase(float radius, bool eraseSelectedTypeOnly, int selectedIndex, size_t paletteSize)
    {
        auto& d = events::EventDispatcher::instance();

        events::meshBrush::SetMeshBrushPaletteCommand paletteCmd;
        paletteCmd.palette = std::vector<meshbrush::MeshPaletteEntry>(paletteSize);
        d.execute(paletteCmd);

        // Event structs derive from INotification (virtual getName) => not aggregates; set fields.
        events::meshBrush::MeshBrushModeChangedNotification activeNotify;
        activeNotify.isActive = true;
        d.publish(activeNotify);

        events::meshBrush::SetMeshBrushModeCommand modeCmd;
        modeCmd.mode = meshbrush::MeshBrushMode::Erase;
        d.execute(modeCmd);

        events::meshBrush::SetMeshBrushSelectedEntryCommand selCmd;
        selCmd.selectedIndex = selectedIndex;
        d.execute(selCmd);

        meshbrush::MeshBrushParams params;
        params.radius = radius;
        params.spacing = 2.0f;
        params.eraseSelectedTypeOnly = eraseSelectedTypeOnly;
        events::meshBrush::SetMeshBrushParamsCommand paramsCmd;
        paramsCmd.params = params;
        d.execute(paramsCmd);
    }

    void eraseAt(const glm::vec3& worldPos)
    {
        events::meshBrush::ApplyMeshBrushCommand cmd;
        cmd.worldPosition = worldPos;
        cmd.surfaceNormal = glm::vec3(0.0f, 1.0f, 0.0f);
        cmd.isFirstApplication = true;
        events::EventDispatcher::instance().execute(cmd);
    }
}

TEST_SUITE("MeshBrushPersistence")
{
    TEST_CASE("MeshBrushInstanceComponent survives scene save/load")
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        asset::AssetDatabase::instance().clear();

        const fs::path dir = fs::temp_directory_path() / "vf_meshbrush_persist_tests";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const fs::path scenePath = dir / "Brush.vfScene";

        const glm::vec3 normal = glm::normalize(glm::vec3(0.3f, 0.8f, -0.5f));

        {
            scene::SceneGraphSystem graph;
            scene::Entity& root = graph.GetRoot();
            scene::Entity inst("BrushInstance");
            auto& comp = inst.addOrReplaceComponent<components::MeshBrushInstanceComponent>();
            comp.brushGroupId = 0xABCDEF01u;
            comp.paletteIndex = 7u;
            comp.surfaceNormal = normal;
            graph.addChild(root, inst);
            REQUIRE(serialization::SceneSerialization::saveScene(graph, scenePath.string()));
            graph.clearScene();
        }

        {
            scene::SceneGraphSystem g;
            REQUIRE(serialization::SceneSerialization::loadSceneInto(scenePath.string(), g));

            int count = 0;
            auto view = registry.view<components::MeshBrushInstanceComponent>();
            for (entt::entity e : view)
            {
                const auto& comp = view.get<components::MeshBrushInstanceComponent>(e);
                CHECK(comp.brushGroupId == 0xABCDEF01u);
                CHECK(comp.paletteIndex == 7u);
                CHECK(comp.surfaceNormal.x == doctest::Approx(normal.x));
                CHECK(comp.surfaceNormal.y == doctest::Approx(normal.y));
                CHECK(comp.surfaceNormal.z == doctest::Approx(normal.z));
                ++count;
            }
            CHECK(count == 1);
            g.clearScene();
        }

        fs::remove_all(dir, ec);
    }

    TEST_CASE("SceneLoaded rebuild places instances at world positions (C1 regression)")
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.clear();

        FakeScene scene;
        installSceneQueryFakes(scene);

        services::MeshBrushServiceImpl service;
        service.registerEventHandlers();

        // Group at sector (0,0) center = (64,0,64); instance local (6,5,6) => world (70,5,70).
        auto group = scene.addGroup(glm::vec3(64.0f, 0.0f, 64.0f));
        auto inst = scene.addInstance(0u, glm::vec3(6.0f, 5.0f, 6.0f), group);

        activateErase(/*radius*/ 2.0f, /*eraseSelectedTypeOnly*/ false, /*selectedIndex*/ -1, /*paletteSize*/ 1);
        dispatcher.publish(events::scene::SceneLoadedNotification{});

        // Erasing at the LOCAL offset (6,5,6) must hit nothing: the grid holds the WORLD position.
        eraseAt(glm::vec3(6.0f, 5.0f, 6.0f));
        CHECK(scene.deleted.empty());

        // Erasing at the true WORLD position removes the instance.
        eraseAt(glm::vec3(70.0f, 5.0f, 70.0f));
        REQUIRE(scene.deleted.size() == 1);
        CHECK(scene.deleted[0].id == inst.id);

        dispatcher.clear();
    }

    TEST_CASE("Erase selected type only removes the selected palette entry")
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.clear();

        FakeScene scene;
        installSceneQueryFakes(scene);

        services::MeshBrushServiceImpl service;
        service.registerEventHandlers();

        // Two instances of different palette types at the same world position (70,5,70).
        auto group = scene.addGroup(glm::vec3(64.0f, 0.0f, 64.0f));
        auto typeA = scene.addInstance(0u, glm::vec3(6.0f, 5.0f, 6.0f), group);
        auto typeB = scene.addInstance(1u, glm::vec3(6.0f, 5.0f, 6.0f), group);

        activateErase(/*radius*/ 5.0f, /*eraseSelectedTypeOnly*/ true, /*selectedIndex*/ 1, /*paletteSize*/ 2);
        dispatcher.publish(events::scene::SceneLoadedNotification{});

        eraseAt(glm::vec3(70.0f, 5.0f, 70.0f));

        REQUIRE(scene.deleted.size() == 1);
        CHECK(scene.deleted[0].id == typeB.id);
        CHECK(scene.deleted[0].id != typeA.id);

        dispatcher.clear();
    }
}
