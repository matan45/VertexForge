#pragma once
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"
#include "data/UndoTypes.hpp"
#include "events/EventDispatcher.hpp"
#include "events/scene/EntityTransformEvents.hpp"
#include <string>
#include <utility>

namespace windows
{
    // VK-1490 — undo for main-scene entity transforms (viewport gizmo drags).
    // Modeled on PrefabRigEntityTransformUndoCommand: captures the handle and
    // before/after TransformData BY VALUE — this lives on the process-global
    // undo stack and outlives every window, so it must hold no pointers —
    // and replaying the Set(World)TransformCommand IS the whole edit. A replay
    // on a now-dead entity safely no-ops (TransformComponentService guards with
    // isValidHandle). worldSpace=true replays SetWorldTransformCommand: group
    // drags record absolute world poses so each top-level member restores
    // correctly regardless of what unselected parents did in between.
    class SceneEntityTransformUndoCommand : public services::IUndoableCommand
    {
    public:
        SceneEntityTransformUndoCommand(services::EntityHandle entity,
                                        services::TransformData before,
                                        services::TransformData after,
                                        bool worldSpace,
                                        std::string description)
            : entity(entity)
            , before(before)
            , after(after)
            , worldSpace(worldSpace)
            , description(std::move(description))
        {
        }

        void execute() override { apply(after); } // redo
        void undo() override { apply(before); }

        std::string getDescription() const override { return description; }

    private:
        void apply(const services::TransformData& t)
        {
            if (worldSpace)
            {
                events::scene::SetWorldTransformCommand cmd;
                cmd.entity = entity;
                cmd.worldTransform = t;
                ::events::EventDispatcher::instance().execute(cmd);
            }
            else
            {
                events::scene::SetTransformCommand cmd;
                cmd.entity = entity;
                cmd.transform = t;
                ::events::EventDispatcher::instance().execute(cmd);
            }
        }

        services::EntityHandle entity;
        services::TransformData before;
        services::TransformData after;
        bool worldSpace;
        std::string description;
    };
}
