#include "PaintModeServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/PaintModeEvents.hpp"
#include "../../events/editor/SculptModeEvents.hpp"
#include "../../events/terrain/HoleModeEvents.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"

namespace services
{
    PaintModeServiceImpl::~PaintModeServiceImpl()
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
        if (holeModeToken.isValid())
        {
            dispatcher.unsubscribe(holeModeToken);
        }
    }

    void PaintModeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::paint::SetPaintModeActiveCommand>(
            [this](const events::paint::SetPaintModeActiveCommand& cmd)
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

        dispatcher.registerQueryHandler<events::paint::IsPaintModeActiveQuery>(
            [this](const events::paint::IsPaintModeActiveQuery&)
            {
                return isActive();
            });

        dispatcher.registerQueryHandler<events::paint::GetPaintTargetEntityQuery>(
            [this](const events::paint::GetPaintTargetEntityQuery&)
            {
                return getTargetEntity();
            });

        editorModeToken = dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& n)
            {
                if (n.currentMode == EditorMode::Play && paintActive)
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
                if (paintActive)
                {
                    deactivate();
                }
            });

        sculptModeToken = dispatcher.subscribe<events::sculpt::SculptModeChangedNotification>(
            [this](const events::sculpt::SculptModeChangedNotification& n)
            {
                if (n.isActive && paintActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when hole mode activates
        holeModeToken = dispatcher.subscribe<events::hole::HoleModeChangedNotification>(
            [this](const events::hole::HoleModeChangedNotification& n)
            {
                if (n.isActive && paintActive)
                {
                    deactivate();
                }
            });
    }

    bool PaintModeServiceImpl::activate()
    {
        if (paintActive)
        {
            return true;
        }

        auto& dispatcher = events::EventDispatcher::instance();

        bool sculptActive = dispatcher.query(events::sculpt::IsSculptModeActiveQuery{});
        if (sculptActive)
        {
            events::sculpt::SetSculptModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }

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
        paintActive = true;

        events::scene::SelectEntityCommand selectCmd;
        selectCmd.entity = terrainEntity;
        dispatcher.execute(selectCmd);

        events::paint::PaintModeChangedNotification notification;
        notification.isActive = true;
        notification.terrainEntity = terrainEntity;
        dispatcher.publish(notification);

        return true;
    }

    void PaintModeServiceImpl::deactivate()
    {
        if (!paintActive)
        {
            return;
        }

        paintActive = false;
        targetTerrain.reset();

        events::paint::PaintModeChangedNotification notification;
        notification.isActive = false;
        notification.terrainEntity = std::nullopt;
        events::EventDispatcher::instance().publish(notification);
    }

    bool PaintModeServiceImpl::isActive() const
    {
        return paintActive;
    }

    std::optional<EntityHandle> PaintModeServiceImpl::getTargetEntity() const
    {
        return targetTerrain;
    }
}
