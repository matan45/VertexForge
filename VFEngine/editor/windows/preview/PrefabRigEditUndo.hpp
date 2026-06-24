#pragma once

// Phase 2 — generic edit undo for the Prefab Rig Preview window.
//
// The window authors three editor-transient rig data sets, each pushed to the controller through
// the existing prefab-rig CQRS:
//   * per-part bone/static SOCKETS  -> SetPrefabRigSocketsCommand (one part at a time)
//   * IK CHAINS (weight/enabled)    -> SetPrefabRigChainsCommand  (whole vector)
//   * per-part preview TRANSFORMS   -> SetPrefabRigPartPreviewTransformCommand (the gizmo offset)
//
// To make every such edit recoverable we snapshot the affected data BEFORE an edit session begins
// and, when the session ends, push ONE IUndoableCommand holding the before/after snapshots. The
// command replays the matching Set* command for each engaged field on undo()/execute(). This is the
// PrefabPreviewWindow analogue of UILayerInspectorUndo (UI Layer Builder).
//
// Entt-free + no new CQRS events: it rides the same per-instance commands the window already sends,
// keyed by the window's PreviewInstanceId, so the rig stays an isolated offscreen sandbox. The
// command also restores the window's OWN mirror state (the previewTransforms map) via a callback so
// the transform gizmo's base anchor stays consistent with the controller after an undo/redo.
//
// IK-binding gotcha: changing an IK chain's target part/socket triggers a full buildPreviewFromDesc()
// rebuild, which reloads sockets/chains from disk. A chains undo therefore re-applies the snapshot's
// sockets for the captured part AFTER replaying the chains, mirroring the IK panel's own re-apply
// sequence, so a rebuild-driven edit does not strand stale socket data.

#include "providers/PreviewInstanceId.hpp"
#include "data/UndoTypes.hpp"
#include "events/EventDispatcher.hpp"
#include "events/render/PrefabRigPreviewEvents.hpp"
#include "animator/SocketTypes.hpp"
#include "animator/IKTypes.hpp"

#include <glm/glm.hpp>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace windows::prefabrigedit
{
    // Value-equality for the authored types (neither defines operator==, and adding one to the
    // shared utility headers would be a wide blast radius). Used only to skip pushing a no-op undo
    // entry, so an approximate-but-strict member compare is the right tradeoff.
    inline bool socketEquals(const animator::SocketDefinition& a, const animator::SocketDefinition& b)
    {
        return a.name == b.name && a.targetBoneName == b.targetBoneName &&
               a.boneIndex == b.boneIndex && a.localPosition == b.localPosition &&
               a.localRotation == b.localRotation;
    }

    inline bool socketsEqual(const std::vector<animator::SocketDefinition>& a,
                             const std::vector<animator::SocketDefinition>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!socketEquals(a[i], b[i])) return false;
        return true;
    }

    inline bool chainEquals(const animator::ik::IKChainConfig& a, const animator::ik::IKChainConfig& b)
    {
        return a.chainName == b.chainName && a.tipBoneName == b.tipBoneName &&
               a.chainBoneNames == b.chainBoneNames && a.weight == b.weight &&
               a.enabled == b.enabled;
    }

    inline bool chainsEqual(const std::vector<animator::ik::IKChainConfig>& a,
                            const std::vector<animator::ik::IKChainConfig>& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!chainEquals(a[i], b[i])) return false;
        return true;
    }

    // One IK chain's editor-transient target BINDING (lives on the window's PrefabRigDescDTO, NOT in
    // IKChainConfig). Captured so a re-target (target part / socket change) is undoable — chainsEqual
    // ignores the binding, so without this a binding change would be a no-op on the undo stack.
    struct IKBindingSnapshot
    {
        int targetPartIndex = -1;
        std::string targetSocketName;
        bool operator==(const IKBindingSnapshot&) const = default;
    };

    // A point-in-time snapshot of the rig data one edit session can touch. Each field is engaged
    // only if that kind of data participated in the session, so a socket-only edit never replays a
    // (no-op) chains command and vice versa. previewTransforms snapshots the WHOLE map because the
    // window keeps its mirror keyed by part and a transform edit restores both the controller and
    // the mirror in one step.
    struct PrefabRigEditSnapshot
    {
        // Sockets for a single part (the part the user was editing). Engaged on socket edits.
        std::optional<int> socketPart;                          // part index the sockets belong to
        std::vector<animator::SocketDefinition> sockets;        // valid iff socketPart engaged

        // IK chains (the whole vector — the panel edits them in place). Engaged on chain edits.
        std::optional<std::vector<animator::ik::IKChainConfig>> chains;

        // IK target bindings, parallel to the chains vector. Engaged when a re-target is undoable.
        // Restored WINDOW-side (onto PrefabRigDescDTO::ik) + a rebuild — there is no binding CQRS.
        std::optional<std::vector<IKBindingSnapshot>> ikBindings;

        // Editor-transient per-part preview transforms (gizmo offsets). Engaged on transform edits.
        std::optional<std::map<int, glm::mat4>> previewTransforms;
    };

    inline bool ikBindingsEqual(const std::vector<IKBindingSnapshot>& a,
                                const std::vector<IKBindingSnapshot>& b)
    {
        return a == b;
    }

    // Restore a snapshot's data onto the live controller via the existing CQRS. `instanceId` keys
    // the per-window controller. Order matters when a single session changed both chains and
    // sockets for the same part (the chains path can rebuild): chains first, then re-apply sockets.
    inline void applyPrefabRigSnapshot(const services::PreviewInstanceId& instanceId,
                                       const PrefabRigEditSnapshot& snap)
    {
        if (snap.chains.has_value())
        {
            services::events::prefabrigpreview::SetPrefabRigChainsCommand cmd;
            cmd.instanceId = instanceId;
            cmd.chains = *snap.chains;
            ::events::EventDispatcher::instance().execute(cmd);
        }

        if (snap.socketPart.has_value())
        {
            services::events::prefabrigpreview::SetPrefabRigSocketsCommand cmd;
            cmd.instanceId = instanceId;
            cmd.part = static_cast<size_t>(*snap.socketPart);
            cmd.sockets = snap.sockets;
            ::events::EventDispatcher::instance().execute(cmd);
        }

        if (snap.previewTransforms.has_value())
        {
            // Reset everything first so parts present in the OTHER snapshot (but absent here) return
            // to identity, then push each captured offset. ResetPrefabRigPreviewTransforms clears
            // the assembly's whole table.
            services::events::prefabrigpreview::ResetPrefabRigPreviewTransformsCommand reset;
            reset.instanceId = instanceId;
            ::events::EventDispatcher::instance().execute(reset);

            for (const auto& [part, m] : *snap.previewTransforms)
            {
                if (m == glm::mat4(1.0f)) continue; // identity == reset already covered it
                services::events::prefabrigpreview::SetPrefabRigPartPreviewTransformCommand cmd;
                cmd.instanceId = instanceId;
                cmd.part = static_cast<size_t>(part);
                cmd.transform = m;
                ::events::EventDispatcher::instance().execute(cmd);
            }
        }
    }

    // Undoable command: restores `before` on undo(), `after` on execute()/redo(). The optional
    // syncWindow callback lets the window re-sync its OWN mirror state (the previewTransforms map +
    // the editSockets/editChains UI copies) to whichever snapshot was just applied, so the gizmo
    // anchor and the panels reflect the restored data.
    //
    // LIFETIME (critical): undo entries live on the process-global undo stack and OUTLIVE the window.
    // The callback must therefore capture ONLY values (e.g. the PreviewInstanceId), NEVER a window
    // pointer/`this` — it resolves the window by id at call time and no-ops if it has been closed.
    // applyPrefabRigSnapshot's CQRS likewise no-ops on a dead instanceId (controller-adapter guards).
    class PrefabRigEditUndoCommand : public services::IUndoableCommand
    {
    public:
        PrefabRigEditUndoCommand(services::PreviewInstanceId instanceId,
                                 PrefabRigEditSnapshot before, PrefabRigEditSnapshot after,
                                 std::string description,
                                 std::function<void(const PrefabRigEditSnapshot&)> syncWindow = {})
            : instanceId(instanceId)
            , before(std::move(before))
            , after(std::move(after))
            , description(std::move(description))
            , syncWindow(std::move(syncWindow))
        {
        }

        void execute() override // redo
        {
            applyPrefabRigSnapshot(instanceId, after);
            if (syncWindow) syncWindow(after);
        }

        void undo() override
        {
            applyPrefabRigSnapshot(instanceId, before);
            if (syncWindow) syncWindow(before);
        }

        std::string getDescription() const override { return description; }

    private:
        services::PreviewInstanceId instanceId;
        PrefabRigEditSnapshot before;
        PrefabRigEditSnapshot after;
        std::string description;
        std::function<void(const PrefabRigEditSnapshot&)> syncWindow;
    };
}
