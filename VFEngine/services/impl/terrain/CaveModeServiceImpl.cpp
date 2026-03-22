#include "CaveModeServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/terrain/CaveModeEvents.hpp"
#include "../../events/editor/SculptModeEvents.hpp"
#include "../../events/terrain/PaintModeEvents.hpp"
#include "../../events/terrain/HoleModeEvents.hpp"
#include "../../events/vegetation/VegetationBrushEvents.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"

namespace services
{
    CaveModeServiceImpl::~CaveModeServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (editorModeToken.isValid()) dispatcher.unsubscribe(editorModeToken);
        if (entityDeletedToken.isValid()) dispatcher.unsubscribe(entityDeletedToken);
        if (sceneClearedToken.isValid()) dispatcher.unsubscribe(sceneClearedToken);
        if (sculptModeToken.isValid()) dispatcher.unsubscribe(sculptModeToken);
        if (paintModeToken.isValid()) dispatcher.unsubscribe(paintModeToken);
        if (holeModeToken.isValid()) dispatcher.unsubscribe(holeModeToken);
        if (vegetationBrushModeToken.isValid()) dispatcher.unsubscribe(vegetationBrushModeToken);
    }

    void CaveModeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::cave::SetCaveModeActiveCommand>(
            [this](const events::cave::SetCaveModeActiveCommand& cmd)
            {
                if (cmd.active)
                    activate();
                else
                    deactivate();
            });

        dispatcher.registerQueryHandler<events::cave::IsCaveModeActiveQuery>(
            [this](const events::cave::IsCaveModeActiveQuery&)
            {
                return isActive();
            });

        dispatcher.registerQueryHandler<events::cave::GetCaveTargetEntityQuery>(
            [this](const events::cave::GetCaveTargetEntityQuery&)
            {
                return getTargetEntity();
            });

        editorModeToken = dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& n)
            {
                if (n.currentMode == EditorMode::Play && caveActive)
                    deactivate();
            });

        entityDeletedToken = dispatcher.subscribe<events::scene::EntityDeletedNotification>(
            [this](const events::scene::EntityDeletedNotification& n)
            {
                if (targetTerrain.has_value() && n.entity == *targetTerrain)
                    deactivate();
            });

        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                if (caveActive) deactivate();
            });

        sculptModeToken = dispatcher.subscribe<events::sculpt::SculptModeChangedNotification>(
            [this](const events::sculpt::SculptModeChangedNotification& n)
            {
                if (n.isActive && caveActive) deactivate();
            });

        paintModeToken = dispatcher.subscribe<events::paint::PaintModeChangedNotification>(
            [this](const events::paint::PaintModeChangedNotification& n)
            {
                if (n.isActive && caveActive) deactivate();
            });

        holeModeToken = dispatcher.subscribe<events::hole::HoleModeChangedNotification>(
            [this](const events::hole::HoleModeChangedNotification& n)
            {
                if (n.isActive && caveActive) deactivate();
            });

        vegetationBrushModeToken = dispatcher.subscribe<events::vegetationBrush::VegetationBrushModeChangedNotification>(
            [this](const events::vegetationBrush::VegetationBrushModeChangedNotification& n)
            {
                if (n.isActive && caveActive) deactivate();
            });
    }

    bool CaveModeServiceImpl::activate()
    {
        if (caveActive)
            return true;

        auto& dispatcher = events::EventDispatcher::instance();

        // Deactivate other modes
        {
            events::sculpt::SetSculptModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }
        {
            events::paint::SetPaintModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }
        {
            events::hole::SetHoleModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }

        auto selectedEntity = dispatcher.query(events::scene::GetSelectedEntityQuery{});
        if (!selectedEntity.has_value())
            return false;

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
                return false;
        }

        targetTerrain = terrainEntity;
        caveActive = true;

        events::scene::SelectEntityCommand selectCmd;
        selectCmd.entity = terrainEntity;
        dispatcher.execute(selectCmd);

        events::cave::CaveModeChangedNotification notification;
        notification.isActive = true;
        notification.terrainEntity = terrainEntity;
        dispatcher.publish(notification);

        return true;
    }

    void CaveModeServiceImpl::deactivate()
    {
        if (!caveActive)
            return;

        caveActive = false;
        targetTerrain.reset();

        events::cave::CaveModeChangedNotification notification;
        notification.isActive = false;
        notification.terrainEntity = std::nullopt;
        events::EventDispatcher::instance().publish(notification);
    }

    bool CaveModeServiceImpl::isActive() const
    {
        return caveActive;
    }

    std::optional<EntityHandle> CaveModeServiceImpl::getTargetEntity() const
    {
        return targetTerrain;
    }
}
