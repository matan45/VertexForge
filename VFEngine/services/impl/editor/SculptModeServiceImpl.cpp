#include "SculptModeServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/editor/SculptModeEvents.hpp"
#include "../../events/terrain/PaintModeEvents.hpp"
#include "../../events/terrain/HoleModeEvents.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"

namespace services
{
    SculptModeServiceImpl::~SculptModeServiceImpl()
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
        if (paintModeToken.isValid())
        {
            dispatcher.unsubscribe(paintModeToken);
        }
        if (holeModeToken.isValid())
        {
            dispatcher.unsubscribe(holeModeToken);
        }
    }

    void SculptModeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::sculpt::SetSculptModeActiveCommand>(
            [this](const events::sculpt::SetSculptModeActiveCommand& cmd)
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

        dispatcher.registerQueryHandler<events::sculpt::IsSculptModeActiveQuery>(
            [this](const events::sculpt::IsSculptModeActiveQuery&)
            {
                return isActive();
            });

        dispatcher.registerQueryHandler<events::sculpt::GetSculptTargetEntityQuery>(
            [this](const events::sculpt::GetSculptTargetEntityQuery&)
            {
                return getTargetEntity();
            });

        // Auto-deactivate on Play mode
        editorModeToken = dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& n)
            {
                if (n.currentMode == EditorMode::Play && sculptActive)
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
                if (sculptActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when paint mode activates
        paintModeToken = dispatcher.subscribe<events::paint::PaintModeChangedNotification>(
            [this](const events::paint::PaintModeChangedNotification& n)
            {
                if (n.isActive && sculptActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when hole mode activates
        holeModeToken = dispatcher.subscribe<events::hole::HoleModeChangedNotification>(
            [this](const events::hole::HoleModeChangedNotification& n)
            {
                if (n.isActive && sculptActive)
                {
                    deactivate();
                }
            });
    }

    bool SculptModeServiceImpl::activate()
    {
        if (sculptActive)
        {
            return true;
        }

        auto& dispatcher = events::EventDispatcher::instance();

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
        sculptActive = true;

        // Force-select the terrain parent entity
        events::scene::SelectEntityCommand selectCmd;
        selectCmd.entity = terrainEntity;
        dispatcher.execute(selectCmd);

        events::sculpt::SculptModeChangedNotification notification;
        notification.isActive = true;
        notification.terrainEntity = terrainEntity;
        dispatcher.publish(notification);

        return true;
    }

    void SculptModeServiceImpl::deactivate()
    {
        if (!sculptActive)
        {
            return;
        }

        sculptActive = false;
        targetTerrain.reset();

        events::sculpt::SculptModeChangedNotification notification;
        notification.isActive = false;
        notification.terrainEntity = std::nullopt;
        events::EventDispatcher::instance().publish(notification);
    }

    bool SculptModeServiceImpl::isActive() const
    {
        return sculptActive;
    }

    std::optional<EntityHandle> SculptModeServiceImpl::getTargetEntity() const
    {
        return targetTerrain;
    }
}
