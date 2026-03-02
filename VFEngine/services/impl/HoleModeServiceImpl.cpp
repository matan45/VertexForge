#include "HoleModeServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/HoleModeEvents.hpp"
#include "../events/SculptModeEvents.hpp"
#include "../events/PaintModeEvents.hpp"
#include "../events/EditorModeEvents.hpp"
#include "../events/SceneEvents.hpp"
#include "../events/TerrainEvents.hpp"

namespace services
{
    HoleModeServiceImpl::~HoleModeServiceImpl()
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
    }

    void HoleModeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::hole::SetHoleModeActiveCommand>(
            [this](const events::hole::SetHoleModeActiveCommand& cmd)
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

        dispatcher.registerQueryHandler<events::hole::IsHoleModeActiveQuery>(
            [this](const events::hole::IsHoleModeActiveQuery&)
            {
                return isActive();
            });

        dispatcher.registerQueryHandler<events::hole::GetHoleTargetEntityQuery>(
            [this](const events::hole::GetHoleTargetEntityQuery&)
            {
                return getTargetEntity();
            });

        // Auto-deactivate on Play mode
        editorModeToken = dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& n)
            {
                if (n.currentMode == EditorMode::Play && holeActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate on target terrain deletion
        entityDeletedToken = dispatcher.subscribe<events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& n)
            {
                if (targetTerrain.has_value() && n.entity == *targetTerrain)
                {
                    deactivate();
                }
            });

        // Auto-deactivate on scene clear
        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                if (holeActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when sculpt mode activates
        sculptModeToken = dispatcher.subscribe<events::sculpt::SculptModeChangedNotification>(
            [this](const events::sculpt::SculptModeChangedNotification& n)
            {
                if (n.isActive && holeActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when paint mode activates
        paintModeToken = dispatcher.subscribe<events::paint::PaintModeChangedNotification>(
            [this](const events::paint::PaintModeChangedNotification& n)
            {
                if (n.isActive && holeActive)
                {
                    deactivate();
                }
            });
    }

    bool HoleModeServiceImpl::activate()
    {
        if (holeActive)
        {
            return true;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        // Deactivate sculpt mode if active
        {
            events::sculpt::SetSculptModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }

        // Deactivate paint mode if active
        {
            events::paint::SetPaintModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }

        auto selectedEntity = dispatcher.query(events::scene::GetSelectedEntityQuery{});
        if (!selectedEntity.has_value())
        {
            return false;
        }

        EntityHandle terrainEntity = *selectedEntity;

        // Check if selected entity is a terrain parent
        events::terrain::HasTerrainComponentQuery terrainQuery;
        terrainQuery.entity = terrainEntity;
        bool isTerrain = dispatcher.query(terrainQuery);

        if (!isTerrain)
        {
            // Check if it's a terrain tile and resolve to parent
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

                    // Verify the parent is actually a terrain
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
        holeActive = true;

        // Force-select the terrain parent entity
        events::scene::SelectEntityCommand selectCmd;
        selectCmd.entity = terrainEntity;
        dispatcher.execute(selectCmd);

        events::hole::HoleModeChangedNotification notification;
        notification.isActive = true;
        notification.terrainEntity = terrainEntity;
        dispatcher.publish(notification);

        return true;
    }

    void HoleModeServiceImpl::deactivate()
    {
        if (!holeActive)
        {
            return;
        }

        holeActive = false;
        targetTerrain.reset();

        events::hole::HoleModeChangedNotification notification;
        notification.isActive = false;
        notification.terrainEntity = std::nullopt;
        events::EventDispatcher::instance().publish(notification);
    }

    bool HoleModeServiceImpl::isActive() const
    {
        return holeActive;
    }

    std::optional<EntityHandle> HoleModeServiceImpl::getTargetEntity() const
    {
        return targetTerrain;
    }
}
