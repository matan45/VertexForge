#pragma once
#include "../../data/UndoTypes.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/scene/ComponentMediaEvents.hpp"
#include "../../../utilities/meshbrush/MeshBrushSpatialGrid.hpp"
#include "../../../utilities/components/MeshBrushComponents.hpp"
#include "../../../utilities/scene/EntityRegistry.hpp"
#include "../../data/EntityConversion.hpp"
#include <vector>
#include <string>
#include <glm/glm.hpp>

namespace services
{
    struct MeshBrushEntitySnapshot
    {
        std::string name;
        std::string meshPath;
        std::string materialPath;
        TransformData transform;
        uint32_t paletteIndex = 0;
        glm::vec3 surfaceNormal{0.0f, 1.0f, 0.0f};
    };

    class MeshBrushPlaceUndoCommand : public IUndoableCommand
    {
    private:
        std::vector<EntityHandle> placedEntities;
        std::vector<MeshBrushEntitySnapshot> snapshots;
        meshbrush::MeshBrushSpatialGrid* spatialGrid;

    public:
        MeshBrushPlaceUndoCommand(std::vector<EntityHandle> entities,
                                   std::vector<MeshBrushEntitySnapshot> snaps,
                                   meshbrush::MeshBrushSpatialGrid* grid)
            : placedEntities(std::move(entities))
            , snapshots(std::move(snaps))
            , spatialGrid(grid)
        {
        }

        void execute() override
        {
            auto& dispatcher = events::EventDispatcher::instance();
            placedEntities.clear();

            for (const auto& snap : snapshots)
            {
                events::scene::CreateEntityCommand createCmd;
                createCmd.name = snap.name;
                auto entity = dispatcher.execute(createCmd);
                if (!entity.isValid()) continue;

                events::scene::AddMeshComponentCommand meshCmd;
                meshCmd.entity = entity;
                dispatcher.execute(meshCmd);

                MeshData meshData;
                meshData.meshPath = snap.meshPath;
                events::scene::SetMeshDataCommand meshDataCmd;
                meshDataCmd.entity = entity;
                meshDataCmd.meshData = meshData;
                dispatcher.execute(meshDataCmd);

                events::scene::SetTransformCommand transformCmd;
                transformCmd.entity = entity;
                transformCmd.transform = snap.transform;
                dispatcher.execute(transformCmd);

                auto enttEntity = internal::fromHandle(entity);
                auto& registry = scene::EntityRegistry::getRegistry();
                registry.emplace<components::MeshBrushInstanceComponent>(enttEntity,
                    components::MeshBrushInstanceComponent{0, snap.paletteIndex, snap.surfaceNormal});

                if (spatialGrid)
                {
                    spatialGrid->insert(entity.id, snap.transform.position);
                }

                placedEntities.push_back(entity);
            }
        }

        void undo() override
        {
            auto& dispatcher = events::EventDispatcher::instance();

            for (const auto& entity : placedEntities)
            {
                if (spatialGrid)
                {
                    spatialGrid->remove(entity.id);
                }

                events::scene::DeleteEntityCommand deleteCmd;
                deleteCmd.entity = entity;
                dispatcher.execute(deleteCmd);
            }

            placedEntities.clear();
        }

        std::string getDescription() const override
        {
            return "Mesh Brush Place (" + std::to_string(snapshots.size()) + " instances)";
        }
    };

    class MeshBrushEraseUndoCommand : public IUndoableCommand
    {
    private:
        std::vector<MeshBrushEntitySnapshot> erasedSnapshots;
        std::vector<EntityHandle> restoredEntities;
        meshbrush::MeshBrushSpatialGrid* spatialGrid;

    public:
        MeshBrushEraseUndoCommand(std::vector<MeshBrushEntitySnapshot> snaps,
                                   meshbrush::MeshBrushSpatialGrid* grid)
            : erasedSnapshots(std::move(snaps))
            , spatialGrid(grid)
        {
        }

        void execute() override
        {
            auto& dispatcher = events::EventDispatcher::instance();

            for (const auto& entity : restoredEntities)
            {
                if (spatialGrid)
                {
                    spatialGrid->remove(entity.id);
                }

                events::scene::DeleteEntityCommand deleteCmd;
                deleteCmd.entity = entity;
                dispatcher.execute(deleteCmd);
            }

            restoredEntities.clear();
        }

        void undo() override
        {
            auto& dispatcher = events::EventDispatcher::instance();
            restoredEntities.clear();

            for (const auto& snap : erasedSnapshots)
            {
                events::scene::CreateEntityCommand createCmd;
                createCmd.name = snap.name;
                auto entity = dispatcher.execute(createCmd);
                if (!entity.isValid()) continue;

                events::scene::AddMeshComponentCommand meshCmd;
                meshCmd.entity = entity;
                dispatcher.execute(meshCmd);

                MeshData meshData;
                meshData.meshPath = snap.meshPath;
                events::scene::SetMeshDataCommand meshDataCmd;
                meshDataCmd.entity = entity;
                meshDataCmd.meshData = meshData;
                dispatcher.execute(meshDataCmd);

                events::scene::SetTransformCommand transformCmd;
                transformCmd.entity = entity;
                transformCmd.transform = snap.transform;
                dispatcher.execute(transformCmd);

                auto enttEntity = internal::fromHandle(entity);
                auto& registry = scene::EntityRegistry::getRegistry();
                registry.emplace<components::MeshBrushInstanceComponent>(enttEntity,
                    components::MeshBrushInstanceComponent{0, snap.paletteIndex, snap.surfaceNormal});

                if (spatialGrid)
                {
                    spatialGrid->insert(entity.id, snap.transform.position);
                }

                restoredEntities.push_back(entity);
            }
        }

        std::string getDescription() const override
        {
            return "Mesh Brush Erase (" + std::to_string(erasedSnapshots.size()) + " instances)";
        }
    };
}
