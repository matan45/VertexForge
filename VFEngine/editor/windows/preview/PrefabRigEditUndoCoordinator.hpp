#pragma once
#include "PrefabRigEditContext.hpp"
#include "PrefabRigEditUndo.hpp" // PrefabRigEditSnapshot + IKBindingSnapshot
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"         // services::TransformData
#include <vector>

namespace windows { class PrefabPreviewWindow; }

namespace windows
{
    // VK-1443 — undo snapshot/push coordinator, split out of PrefabPreviewWindow. Owns ONLY the
    // snapshot*/push* helpers; the live-window registry, resyncMirror/resyncRebuild (statics) and
    // applyMirrorSnapshot stay on the window (lowest-risk replay path). The pushed commands capture
    // ONLY the PreviewInstanceId (a value) + snapshots, never a window pointer, so they are safe on the
    // process-global undo stack after the window closes.
    class PrefabRigEditUndoCoordinator
    {
    public:
        explicit PrefabRigEditUndoCoordinator(PrefabRigEditContext& ctx, PrefabPreviewWindow& w)
            : ctx(ctx), w(w) {}

        // Build a snapshot of the data a given edit kind touches (engages only the relevant fields).
        prefabrigedit::PrefabRigEditSnapshot snapshotSockets() const;   // editSockets for selectedPart
        prefabrigedit::PrefabRigEditSnapshot snapshotChains() const;    // editChains + IK bindings
        std::vector<prefabrigedit::IKBindingSnapshot> snapshotIKBindings() const; // rigDesc.ik bindings

        // Push a single coalesced undo entry (no-op if before == after data).
        void pushSocketUndo(prefabrigedit::PrefabRigEditSnapshot before);
        void pushChainUndo(prefabrigedit::PrefabRigEditSnapshot before);
        // Push a PrefabRigEntityTransformUndoCommand for `entity` (no-op if before == after).
        void pushTransformUndo(services::EntityHandle entity,
                               const services::TransformData& before,
                               const services::TransformData& after);

    private:
        PrefabRigEditContext& ctx;
        PrefabPreviewWindow& w;
    };
}
