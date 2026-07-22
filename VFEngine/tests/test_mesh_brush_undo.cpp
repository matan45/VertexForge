#include <doctest.h>

#include "data/MeshBrushUndoCommands.hpp"
#include "events/EventDispatcher.hpp"
#include "events/meshbrush/MeshBrushEvents.hpp"

#include <map>
#include <utility>

namespace
{
    using DeltaCommand = events::meshBrush::ApplyMeshBrushInstanceDeltaCommand;

    struct DeltaHandlerGuard
    {
        ~DeltaHandlerGuard()
        {
            events::EventDispatcher::instance().unregisterCommandHandler<DeltaCommand>();
        }
    };

    meshbrush::MeshBrushInstanceSpec makeSpec(uint64_t id, float offset)
    {
        meshbrush::MeshBrushInstanceSpec spec;
        spec.instanceId = id;
        spec.paletteIndex = static_cast<uint32_t>(id + 10);
        spec.worldPosition = {offset + 1.0f, offset + 2.0f, offset + 3.0f};
        spec.rotation = {offset + 4.0f, offset + 5.0f, offset + 6.0f};
        spec.scale = {offset + 0.5f, offset + 0.75f, offset + 1.0f};
        spec.meshPath = "meshes/mesh_" + std::to_string(id) + ".vfMesh";
        spec.materialPath = "materials/material_" + std::to_string(id) + ".vfMaterial";
        spec.useCollider = (id % 2) == 0;
        return spec;
    }

    void checkSpec(const meshbrush::MeshBrushInstanceSpec& actual,
                   const meshbrush::MeshBrushInstanceSpec& expected)
    {
        CHECK(actual.instanceId == expected.instanceId);
        CHECK(actual.paletteIndex == expected.paletteIndex);
        CHECK(actual.worldPosition.x == doctest::Approx(expected.worldPosition.x));
        CHECK(actual.worldPosition.y == doctest::Approx(expected.worldPosition.y));
        CHECK(actual.worldPosition.z == doctest::Approx(expected.worldPosition.z));
        CHECK(actual.rotation.x == doctest::Approx(expected.rotation.x));
        CHECK(actual.rotation.y == doctest::Approx(expected.rotation.y));
        CHECK(actual.rotation.z == doctest::Approx(expected.rotation.z));
        CHECK(actual.scale.x == doctest::Approx(expected.scale.x));
        CHECK(actual.scale.y == doctest::Approx(expected.scale.y));
        CHECK(actual.scale.z == doctest::Approx(expected.scale.z));
        CHECK(actual.meshPath == expected.meshPath);
        CHECK(actual.materialPath == expected.materialPath);
        CHECK(actual.useCollider == expected.useCollider);
    }

    DeltaHandlerGuard installStoreHandler(
        std::map<uint64_t, meshbrush::MeshBrushInstanceSpec>& store)
    {
        auto& dispatcher = events::EventDispatcher::instance();
        dispatcher.unregisterCommandHandler<DeltaCommand>();
        dispatcher.registerCommandHandler<DeltaCommand>(
            [&store](const DeltaCommand& command)
            {
                for (uint64_t id : command.removeIds)
                    store.erase(id);
                for (const auto& spec : command.respawnSpecs)
                    store[spec.instanceId] = spec;
            });
        return {};
    }
}

TEST_SUITE("MeshBrushUndo")
{
    TEST_CASE("mesh_brush_undo: empty stroke has no changes")
    {
        services::MeshBrushStrokeUndoCommand command({}, {});
        CHECK_FALSE(command.hasChanges());
        CHECK(command.getDescription() == "Edit Mesh Brush");
    }

    TEST_CASE("mesh_brush_undo: paint stroke removes then restores exact specs")
    {
        std::map<uint64_t, meshbrush::MeshBrushInstanceSpec> store;
        auto guard = installStoreHandler(store);
        const auto first = makeSpec(1, 10.0f);
        const auto second = makeSpec(2, 20.0f);
        store.emplace(first.instanceId, first);
        store.emplace(second.instanceId, second);

        services::MeshBrushStrokeUndoCommand command({first, second}, {});
        REQUIRE(command.hasChanges());
        CHECK(command.getDescription() == "Paint Mesh Brush");

        command.undo();
        CHECK(store.empty());

        command.execute();
        REQUIRE(store.size() == 2);
        checkSpec(store.at(first.instanceId), first);
        checkSpec(store.at(second.instanceId), second);
    }

    TEST_CASE("mesh_brush_undo: erase stroke restores then removes exact specs")
    {
        std::map<uint64_t, meshbrush::MeshBrushInstanceSpec> store;
        auto guard = installStoreHandler(store);
        const auto first = makeSpec(3, 30.0f);
        const auto second = makeSpec(4, 40.0f);

        services::MeshBrushStrokeUndoCommand command({}, {first, second});
        REQUIRE(command.hasChanges());
        CHECK(command.getDescription() == "Erase Mesh Brush");

        command.undo();
        REQUIRE(store.size() == 2);
        checkSpec(store.at(first.instanceId), first);
        checkSpec(store.at(second.instanceId), second);

        command.execute();
        CHECK(store.empty());
    }

    TEST_CASE("mesh_brush_undo: mixed stroke applies removal before respawn")
    {
        std::map<uint64_t, meshbrush::MeshBrushInstanceSpec> store;
        auto guard = installStoreHandler(store);
        const auto created = makeSpec(5, 50.0f);
        const auto removed = makeSpec(6, 60.0f);
        store.emplace(removed.instanceId, removed);

        services::MeshBrushStrokeUndoCommand command({created}, {removed});
        CHECK(command.getDescription() == "Edit Mesh Brush");

        command.execute();
        REQUIRE(store.size() == 1);
        checkSpec(store.at(created.instanceId), created);

        command.undo();
        REQUIRE(store.size() == 1);
        checkSpec(store.at(removed.instanceId), removed);
    }
}
