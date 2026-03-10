#include "MeshBrushModeServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/meshbrush/MeshBrushEvents.hpp"
#include "../../events/editor/EditorModeEvents.hpp"
#include "../../events/project/SceneEvents.hpp"
#include "../../events/editor/SculptModeEvents.hpp"
#include "../../events/terrain/PaintModeEvents.hpp"
#include "../../events/terrain/HoleModeEvents.hpp"
#include "../../events/vegetation/VegetationBrushEvents.hpp"

namespace services
{
    MeshBrushModeServiceImpl::~MeshBrushModeServiceImpl()
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
        if (vegetationModeToken.isValid())
            dispatcher.unsubscribe(vegetationModeToken);
    }

    void MeshBrushModeServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::meshBrush::SetMeshBrushModeActiveCommand>(
            [this](const events::meshBrush::SetMeshBrushModeActiveCommand& cmd)
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

        dispatcher.registerQueryHandler<events::meshBrush::IsMeshBrushModeActiveQuery>(
            [this](const events::meshBrush::IsMeshBrushModeActiveQuery&)
            {
                return isActive();
            });

        // Auto-deactivate when editor mode changes to Play
        editorModeToken = dispatcher.subscribe<events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& n)
            {
                if (n.currentMode == EditorMode::Play && meshBrushActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when scene is cleared
        sceneClearedToken = dispatcher.subscribe<events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                if (meshBrushActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when sculpt mode activates
        sculptModeToken = dispatcher.subscribe<events::sculpt::SculptModeChangedNotification>(
            [this](const events::sculpt::SculptModeChangedNotification& n)
            {
                if (n.isActive && meshBrushActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when paint mode activates
        paintModeToken = dispatcher.subscribe<events::paint::PaintModeChangedNotification>(
            [this](const events::paint::PaintModeChangedNotification& n)
            {
                if (n.isActive && meshBrushActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when hole mode activates
        holeModeToken = dispatcher.subscribe<events::hole::HoleModeChangedNotification>(
            [this](const events::hole::HoleModeChangedNotification& n)
            {
                if (n.isActive && meshBrushActive)
                {
                    deactivate();
                }
            });

        // Auto-deactivate when vegetation mode activates
        vegetationModeToken = dispatcher.subscribe<events::vegetationBrush::VegetationBrushModeChangedNotification>(
            [this](const events::vegetationBrush::VegetationBrushModeChangedNotification& n)
            {
                if (n.isActive && meshBrushActive)
                {
                    deactivate();
                }
            });
    }

    void MeshBrushModeServiceImpl::activate()
    {
        if (meshBrushActive)
        {
            return;
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

        // Deactivate vegetation mode if active
        bool vegActive = dispatcher.query(events::vegetationBrush::IsVegetationBrushModeActiveQuery{});
        if (vegActive)
        {
            events::vegetationBrush::SetVegetationBrushModeActiveCommand cmd;
            cmd.active = false;
            dispatcher.execute(cmd);
        }

        meshBrushActive = true;

        events::meshBrush::MeshBrushModeChangedNotification notification;
        notification.isActive = true;
        dispatcher.publish(notification);
    }

    void MeshBrushModeServiceImpl::deactivate()
    {
        if (!meshBrushActive)
        {
            return;
        }

        meshBrushActive = false;

        events::meshBrush::MeshBrushModeChangedNotification notification;
        notification.isActive = false;
        events::EventDispatcher::instance().publish(notification);
    }

    bool MeshBrushModeServiceImpl::isActive() const
    {
        return meshBrushActive;
    }
}
