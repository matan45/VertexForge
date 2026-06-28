#include "PrefabRigEditUndoCoordinator.hpp"
#include "PrefabPreviewWindow.hpp"
#include "events/EventDispatcher.hpp"
#include "events/editor/UndoRedoEvents.hpp"
#include <memory>
#include <utility>

namespace windows
{
    // ----------------------------------------------------------------------
    // Phase 2: edit-undo snapshot / push helpers
    // ----------------------------------------------------------------------
    prefabrigedit::PrefabRigEditSnapshot PrefabRigEditUndoCoordinator::snapshotSockets() const
    {
        prefabrigedit::PrefabRigEditSnapshot snap;
        snap.socketPart = ctx.selectedPart;
        snap.sockets = ctx.editSockets;
        return snap;
    }

    std::vector<prefabrigedit::IKBindingSnapshot> PrefabRigEditUndoCoordinator::snapshotIKBindings() const
    {
        std::vector<prefabrigedit::IKBindingSnapshot> out;
        out.reserve(ctx.rigDesc.ik.size());
        for (const auto& ik : ctx.rigDesc.ik)
            out.push_back({ik.targetPartIndex, ik.targetSocketName});
        return out;
    }

    prefabrigedit::PrefabRigEditSnapshot PrefabRigEditUndoCoordinator::snapshotChains() const
    {
        prefabrigedit::PrefabRigEditSnapshot snap;
        snap.chains = ctx.editChains;
        snap.ikBindings = snapshotIKBindings(); // carry the transient target bindings too
        return snap;
    }

    void PrefabRigEditUndoCoordinator::pushSocketUndo(prefabrigedit::PrefabRigEditSnapshot before)
    {
        prefabrigedit::PrefabRigEditSnapshot after = snapshotSockets();
        if (before.socketPart == after.socketPart &&
            prefabrigedit::socketsEqual(before.sockets, after.sockets))
            return; // no actual change — don't pollute the stack

        const services::PreviewInstanceId id = ctx.instanceId;
        // Capture ONLY the id (a value) — never `this`. resyncMirror looks the window up by id and
        // no-ops if it has been closed, so the command is safe on the process-global undo stack.
        auto cmd = std::make_shared<prefabrigedit::PrefabRigEditUndoCommand>(
            id, std::move(before), std::move(after), "Edit rig sockets",
            [id](const prefabrigedit::PrefabRigEditSnapshot& snap)
            {
                PrefabPreviewWindow::resyncMirror(id, snap);
            });

        events::undoredo::PushUndoableCommand push;
        push.command = std::make_shared<services::SharedUndoCommand>(std::move(cmd));
        events::EventDispatcher::instance().execute(push);
    }

    void PrefabRigEditUndoCoordinator::pushChainUndo(prefabrigedit::PrefabRigEditSnapshot before)
    {
        prefabrigedit::PrefabRigEditSnapshot after = snapshotChains();

        // SHOULD-FIX #2: the IK target BINDING (rigDesc.ik[i].targetPartIndex/targetSocketName) is
        // NOT part of IKChainConfig, so chainsEqual ignores it. snapshotChains() captures the bindings
        // too (both before, at session start, and after here) so a re-target is undoable.
        const bool chainsChanged = !(before.chains.has_value() && after.chains.has_value() &&
                                     prefabrigedit::chainsEqual(*before.chains, *after.chains));
        const bool bindingsChanged = !(before.ikBindings.has_value() && after.ikBindings.has_value() &&
                                       prefabrigedit::ikBindingsEqual(*before.ikBindings, *after.ikBindings));
        if (!chainsChanged && !bindingsChanged)
            return; // nothing actually changed — don't pollute the stack

        // A chain undo restores chains AND re-applies the currently selected part's sockets, because
        // a target-binding change rebuilds the assembly (reloading sockets from disk). Carry the live
        // sockets along so an undo/redo that touched a binding does not strand stale socket data.
        if (ctx.selectedPart >= 0 && !ctx.editSockets.empty())
        {
            before.socketPart = ctx.selectedPart;
            before.sockets = ctx.editSockets; // restored state mirrors what's live now
            after.socketPart = ctx.selectedPart;
            after.sockets = ctx.editSockets;
        }

        const services::PreviewInstanceId id = ctx.instanceId;
        auto cmd = std::make_shared<prefabrigedit::PrefabRigEditUndoCommand>(
            id, std::move(before), std::move(after), "Edit IK chains",
            [id](const prefabrigedit::PrefabRigEditSnapshot& snap)
            {
                PrefabPreviewWindow::resyncMirror(id, snap);
            });

        events::undoredo::PushUndoableCommand push;
        push.command = std::make_shared<services::SharedUndoCommand>(std::move(cmd));
        events::EventDispatcher::instance().execute(push);
    }

    void PrefabRigEditUndoCoordinator::pushTransformUndo(services::EntityHandle entity,
                                                         const services::TransformData& before,
                                                         const services::TransformData& after)
    {
        if (!entity.isValid() || before == after)
            return; // nothing actually changed — don't pollute the stack

        const services::PreviewInstanceId id = ctx.instanceId;
        // Capture ONLY the id (a value) — never `this`. resyncRebuild looks the window up by id and
        // no-ops if it has been closed, so the command is safe on the process-global undo stack.
        auto cmd = std::make_shared<prefabrigedit::PrefabRigEntityTransformUndoCommand>(
            entity, before, after, "Edit part transform",
            [id]() { PrefabPreviewWindow::resyncRebuild(id); });

        events::undoredo::PushUndoableCommand push;
        push.command = std::make_shared<services::SharedUndoCommand>(std::move(cmd));
        events::EventDispatcher::instance().execute(push);
    }
}
