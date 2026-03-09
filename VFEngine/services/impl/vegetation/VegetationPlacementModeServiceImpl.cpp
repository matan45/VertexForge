#include "VegetationPlacementModeServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/vegetation/VegetationBrushEvents.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/editor/SculptModeEvents.hpp"
#include "../../events/terrain/PaintModeEvents.hpp"
#include "../../events/terrain/HoleModeEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"

namespace services
{
    VegetationPlacementModeServiceImpl::~VegetationPlacementModeServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (editorModeToken.isValid()) dispatcher.unsubscribe(editorModeToken);
        if (entityDeletedToken.isValid()) dispatcher.unsubscribe(entityDeletedToken);
        if (sceneClearedToken.isValid()) dispatcher.unsubscribe(sceneClearedToken);
        if (sculptModeToken.isValid()) dispatcher.unsubscribe(sculptModeToken);
        if (paintModeToken.isValid()) dispatcher.unsubscribe(paintModeToken);
        if (holeModeToken.isValid()) dispatcher.unsubscribe(holeModeToken);
        if (vegBrushModeToken.isValid()) dispatcher.unsubscribe(vegBrushModeToken);
    }

    void VegetationPlacementModeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::vegetationBrush::SetVegetationPlacementModeActiveCommand>(
            [this](const events::vegetationBrush::SetVegetationPlacementModeActiveCommand& cmd)
            {
                if (cmd.active)
                {
                    activate();
                }
                else
                {
                    deactivate();
                }
            });

        dispatcher.registerQueryHandler<events::vegetationBrush::IsVegetationPlacementModeActiveQuery>(
            [this](const events::vegetationBrush::IsVegetationPlacementModeActiveQuery&)
            {
                return isActive();
            });

        dispatcher.registerQueryHandler<events::vegetationBrush::GetVegetationPlacementTargetEntityQuery>(
            [this](const events::vegetationBrush::GetVegetationPlacementTargetEntityQuery&)
            {
                return getTargetEntity();
            });

        editorModeToken = dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& n)
            {
                if (n.currentMode == EditorMode::Play && placementActive)
                {
                    deactivate();
                }
            });

        entityDeletedToken = dispatcher.subscribe<events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& n)
            {
                if (targetTerrain.has_value() && n.entity == *targetTerrain)
                {
                    deactivate();
                }
            });

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                if (placementActive)
                {
                    deactivate();
                }
            });

        sculptModeToken = dispatcher.subscribe<events::sculpt::SculptModeChangedNotification>(
            [this](const events::sculpt::SculptModeChangedNotification& n)
            {
                if (n.isActive && placementActive) deactivate();
            });

        paintModeToken = dispatcher.subscribe<events::paint::PaintModeChangedNotification>(
            [this](const events::paint::PaintModeChangedNotification& n)
            {
                if (n.isActive && placementActive) deactivate();
            });

        holeModeToken = dispatcher.subscribe<events::hole::HoleModeChangedNotification>(
            [this](const events::hole::HoleModeChangedNotification& n)
            {
                if (n.isActive && placementActive) deactivate();
            });

        vegBrushModeToken = dispatcher.subscribe<events::vegetationBrush::VegetationBrushModeChangedNotification>(
            [this](const events::vegetationBrush::VegetationBrushModeChangedNotification& n)
            {
                if (n.isActive && placementActive) deactivate();
            });
    }

    bool VegetationPlacementModeServiceImpl::activate()
    {
        if (placementActive) return true;

        auto& dispatcher = events::EventDispatcher::instance();

        // Deactivate other terrain modes
        if (dispatcher.query(events::sculpt::IsSculptModeActiveQuery{}))
        {
            events::sculpt::SetSculptModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }
        if (dispatcher.query(events::paint::IsPaintModeActiveQuery{}))
        {
            events::paint::SetPaintModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }
        if (dispatcher.query(events::hole::IsHoleModeActiveQuery{}))
        {
            events::hole::SetHoleModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }
        if (dispatcher.query(events::vegetationBrush::IsVegetationBrushModeActiveQuery{}))
        {
            events::vegetationBrush::SetVegetationBrushModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }

        // Get selected entity and verify it is a terrain
        auto selectedEntity = dispatcher.query(events::scene::GetSelectedEntityQuery{});
        if (!selectedEntity.has_value()) return false;

        EntityHandle terrainEntity = *selectedEntity;

        events::terrain::HasTerrainComponentQuery terrainQuery;
        terrainQuery.entity = terrainEntity;
        bool isTerrain = dispatcher.query(terrainQuery);

        if (!isTerrain)
        {
            events::terrain::HasTerrainTileComponentQuery tileQuery;
            tileQuery.entity = terrainEntity;
            bool isTile = dispatcher.query(tileQuery);

            if (isTile)
            {
                events::scene::GetEntityQuery entityQuery;
                entityQuery.entity = terrainEntity;
                auto entityData = dispatcher.query(entityQuery);

                if (entityData.has_value() && entityData->parent.has_value())
                {
                    terrainEntity = *entityData->parent;

                    events::terrain::HasTerrainComponentQuery parentTerrainQuery;
                    parentTerrainQuery.entity = terrainEntity;
                    isTerrain = dispatcher.query(parentTerrainQuery);
                }
            }

            if (!isTerrain) return false;
        }

        targetTerrain = terrainEntity;
        placementActive = true;

        events::scene::SelectEntityCommand selectCmd;
        selectCmd.entity = terrainEntity;
        dispatcher.execute(selectCmd);

        events::vegetationBrush::VegetationPlacementModeChangedNotification notification;
        notification.isActive = true;
        notification.terrainEntity = terrainEntity;
        dispatcher.publish(notification);

        return true;
    }

    void VegetationPlacementModeServiceImpl::deactivate()
    {
        if (!placementActive) return;

        placementActive = false;
        targetTerrain.reset();

        events::vegetationBrush::VegetationPlacementModeChangedNotification notification;
        notification.isActive = false;
        notification.terrainEntity = std::nullopt;
        events::EventDispatcher::instance().publish(notification);
    }

    bool VegetationPlacementModeServiceImpl::isActive() const
    {
        return placementActive;
    }

    std::optional<EntityHandle> VegetationPlacementModeServiceImpl::getTargetEntity() const
    {
        return targetTerrain;
    }
}
