#include "VegetationBrushModeServiceImpl.hpp"
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
    VegetationBrushModeServiceImpl::~VegetationBrushModeServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (editorModeToken.isValid())
        {
            dispatcher.unsubscribe(editorModeToken);
        }
        if (entityDeletedToken.isValid())
        {
            dispatcher.unsubscribe(entityDeletedToken);
        }
        if (sceneClearedToken.isValid())
        {
            dispatcher.unsubscribe(sceneClearedToken);
        }
        if (sculptModeToken.isValid())
        {
            dispatcher.unsubscribe(sculptModeToken);
        }
        if (paintModeToken.isValid())
        {
            dispatcher.unsubscribe(paintModeToken);
        }
        if (holeModeToken.isValid())
        {
            dispatcher.unsubscribe(holeModeToken);
        }
        if (vegPlacementModeToken.isValid())
        {
            dispatcher.unsubscribe(vegPlacementModeToken);
        }
    }

    void VegetationBrushModeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::vegetationBrush::SetVegetationBrushModeActiveCommand>(
            [this](const events::vegetationBrush::SetVegetationBrushModeActiveCommand& cmd)
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

        dispatcher.registerQueryHandler<events::vegetationBrush::IsVegetationBrushModeActiveQuery>(
            [this](const events::vegetationBrush::IsVegetationBrushModeActiveQuery&)
            {
                return isActive();
            });

        dispatcher.registerQueryHandler<events::vegetationBrush::GetVegetationBrushTargetEntityQuery>(
            [this](const events::vegetationBrush::GetVegetationBrushTargetEntityQuery&)
            {
                return getTargetEntity();
            });

        // Auto-deactivate when editor mode changes to Play
        editorModeToken = dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& n)
            {
                if (n.currentMode == EditorMode::Play && vegetationBrushActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when target entity is deleted
        entityDeletedToken = dispatcher.subscribe<events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& n)
            {
                if (targetTerrain.has_value() && n.entity == *targetTerrain)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when scene is cleared
        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                if (vegetationBrushActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when sculpt mode activates
        sculptModeToken = dispatcher.subscribe<events::sculpt::SculptModeChangedNotification>(
            [this](const events::sculpt::SculptModeChangedNotification& n)
            {
                if (n.isActive && vegetationBrushActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when paint mode activates
        paintModeToken = dispatcher.subscribe<events::paint::PaintModeChangedNotification>(
            [this](const events::paint::PaintModeChangedNotification& n)
            {
                if (n.isActive && vegetationBrushActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when hole mode activates
        holeModeToken = dispatcher.subscribe<events::hole::HoleModeChangedNotification>(
            [this](const events::hole::HoleModeChangedNotification& n)
            {
                if (n.isActive && vegetationBrushActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when vegetation placement mode activates
        vegPlacementModeToken = dispatcher.subscribe<events::vegetationBrush::VegetationPlacementModeChangedNotification>(
            [this](const events::vegetationBrush::VegetationPlacementModeChangedNotification& n)
            {
                if (n.isActive && vegetationBrushActive)
                {
                    deactivate();
                }
            });
    }

    bool VegetationBrushModeServiceImpl::activate()
    {
        if (vegetationBrushActive)
        {
            return true;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // Deactivate sculpt mode if active
        bool sculptActive = dispatcher.query(events::sculpt::IsSculptModeActiveQuery{});
        if (sculptActive)
        {
            events::sculpt::SetSculptModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }

        // Deactivate paint mode if active
        bool paintActive = dispatcher.query(events::paint::IsPaintModeActiveQuery{});
        if (paintActive)
        {
            events::paint::SetPaintModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }

        // Deactivate hole mode if active
        bool holeActive = dispatcher.query(events::hole::IsHoleModeActiveQuery{});
        if (holeActive)
        {
            events::hole::SetHoleModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }

        // Deactivate vegetation placement mode if active
        bool vegPlacementActive = dispatcher.query(events::vegetationBrush::IsVegetationPlacementModeActiveQuery{});
        if (vegPlacementActive)
        {
            events::vegetationBrush::SetVegetationPlacementModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }

        // Get selected entity and verify it is a terrain
        auto selectedEntity = dispatcher.query(events::scene::GetSelectedEntityQuery{});
        if (!selectedEntity.has_value())
        {
            return false;
        }

        EntityHandle terrainEntity = *selectedEntity;

        events::terrain::HasTerrainComponentQuery terrainQuery;
        terrainQuery.entity = terrainEntity;
        bool isTerrain = dispatcher.query(terrainQuery);

        if (!isTerrain)
        {
            // Check if selected entity is a terrain tile child
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

            if (!isTerrain)
            {
                return false;
            }
        }

        targetTerrain = terrainEntity;
        vegetationBrushActive = true;

        events::scene::SelectEntityCommand selectCmd;
        selectCmd.entity = terrainEntity;
        dispatcher.execute(selectCmd);

        events::vegetationBrush::VegetationBrushModeChangedNotification notification;
        notification.isActive = true;
        notification.terrainEntity = terrainEntity;
        dispatcher.publish(notification);

        return true;
    }

    void VegetationBrushModeServiceImpl::deactivate()
    {
        if (!vegetationBrushActive)
        {
            return;
        }

        vegetationBrushActive = false;
        targetTerrain.reset();

        events::vegetationBrush::VegetationBrushModeChangedNotification notification;
        notification.isActive = false;
        notification.terrainEntity = std::nullopt;
        events::EventDispatcher::instance().publish(notification);
    }

    bool VegetationBrushModeServiceImpl::isActive() const
    {
        return vegetationBrushActive;
    }

    std::optional<EntityHandle> VegetationBrushModeServiceImpl::getTargetEntity() const
    {
        return targetTerrain;
    }
}
