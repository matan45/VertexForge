#include "FoliageBrushModeServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/foliage/FoliageBrushEvents.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/editor/SculptModeEvents.hpp"
#include "../../events/terrain/PaintModeEvents.hpp"
#include "../../events/terrain/HoleModeEvents.hpp"
#include "../../events/terrain/CaveModeEvents.hpp"
#include "../../events/vegetation/VegetationBrushEvents.hpp"
#include "../../events/meshbrush/MeshBrushEvents.hpp"

namespace services
{
    FoliageBrushModeServiceImpl::~FoliageBrushModeServiceImpl()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        if (editorModeToken.isValid())
            dispatcher.unsubscribe(editorModeToken);
        if (sceneClearedToken.isValid())
            dispatcher.unsubscribe(sceneClearedToken);
        if (sculptModeToken.isValid())
            dispatcher.unsubscribe(sculptModeToken);
        if (paintModeToken.isValid())
            dispatcher.unsubscribe(paintModeToken);
        if (holeModeToken.isValid())
            dispatcher.unsubscribe(holeModeToken);
        if (caveModeToken.isValid())
            dispatcher.unsubscribe(caveModeToken);
        if (vegetationBrushModeToken.isValid())
            dispatcher.unsubscribe(vegetationBrushModeToken);
        if (meshBrushModeToken.isValid())
            dispatcher.unsubscribe(meshBrushModeToken);
    }

    void FoliageBrushModeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::foliageBrush::SetFoliageBrushModeActiveCommand>(
            [this](const events::foliageBrush::SetFoliageBrushModeActiveCommand& cmd)
            {
                if (cmd.active)
                    activate();
                else
                    deactivate();
            });

        dispatcher.registerQueryHandler<events::foliageBrush::IsFoliageBrushModeActiveQuery>(
            [this](const events::foliageBrush::IsFoliageBrushModeActiveQuery&)
            {
                return isActive();
            });

        // Auto-deactivate when editor mode changes to Play.
        editorModeToken = dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& n)
            {
                if (n.currentMode == EditorMode::Play && foliageBrushActive)
                    deactivate();
            });

        // Auto-deactivate when the scene is cleared.
        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                if (foliageBrushActive)
                    deactivate();
            });

        // Auto-deactivate when any sibling brush/terrain mode activates.
        sculptModeToken = dispatcher.subscribe<events::sculpt::SculptModeChangedNotification>(
            [this](const events::sculpt::SculptModeChangedNotification& n)
            {
                if (n.isActive && foliageBrushActive)
                    deactivate();
            });

        paintModeToken = dispatcher.subscribe<events::paint::PaintModeChangedNotification>(
            [this](const events::paint::PaintModeChangedNotification& n)
            {
                if (n.isActive && foliageBrushActive)
                    deactivate();
            });

        holeModeToken = dispatcher.subscribe<events::hole::HoleModeChangedNotification>(
            [this](const events::hole::HoleModeChangedNotification& n)
            {
                if (n.isActive && foliageBrushActive)
                    deactivate();
            });

        caveModeToken = dispatcher.subscribe<events::cave::CaveModeChangedNotification>(
            [this](const events::cave::CaveModeChangedNotification& n)
            {
                if (n.isActive && foliageBrushActive)
                    deactivate();
            });

        vegetationBrushModeToken = dispatcher.subscribe<events::vegetationBrush::VegetationBrushModeChangedNotification>(
            [this](const events::vegetationBrush::VegetationBrushModeChangedNotification& n)
            {
                if (n.isActive && foliageBrushActive)
                    deactivate();
            });

        meshBrushModeToken = dispatcher.subscribe<events::meshBrush::MeshBrushModeChangedNotification>(
            [this](const events::meshBrush::MeshBrushModeChangedNotification& n)
            {
                if (n.isActive && foliageBrushActive)
                    deactivate();
            });
    }

    void FoliageBrushModeServiceImpl::activate()
    {
        if (foliageBrushActive)
            return;

        auto& dispatcher = events::EventDispatcher::instance();

        // Deactivate every sibling mode that is currently active. Siblings also self-
        // deactivate off the FoliageBrushModeChangedNotification published below, but the
        // explicit commands mirror the vegetation/mesh idiom and keep exclusion robust.
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
        if (dispatcher.query(events::cave::IsCaveModeActiveQuery{}))
        {
            events::cave::SetCaveModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }
        if (dispatcher.query(events::vegetationBrush::IsVegetationBrushModeActiveQuery{}))
        {
            events::vegetationBrush::SetVegetationBrushModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }
        if (dispatcher.query(events::meshBrush::IsMeshBrushModeActiveQuery{}))
        {
            events::meshBrush::SetMeshBrushModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }

        foliageBrushActive = true;

        events::foliageBrush::FoliageBrushModeChangedNotification notification;
        notification.isActive = true;
        dispatcher.publish(notification);
    }

    void FoliageBrushModeServiceImpl::deactivate()
    {
        if (!foliageBrushActive)
            return;

        foliageBrushActive = false;

        events::foliageBrush::FoliageBrushModeChangedNotification notification;
        notification.isActive = false;
        events::EventDispatcher::instance().publish(notification);
    }

    bool FoliageBrushModeServiceImpl::isActive() const
    {
        return foliageBrushActive;
    }
}
