#pragma once
#include "PrefabRigEditContext.hpp"
#include "PrefabRigEditUndo.hpp" // PrefabRigEditSnapshot (edit-bracket before-state)
#include "data/DTOs.hpp"         // services::TransformData (transform gizmo bracket)
#include "imgui.h"               // ImVec2 (viewport gizmo / bone-pick rects)
#include "ImGuizmo.h"
#include <string>
#include <glm/glm.hpp>

namespace windows { class PrefabPreviewWindow; }

namespace windows
{
    // VK-1443 — viewport gizmos + authoring panels (Transform / State / Bone Socket / Static Socket /
    // IK) sub-controller, split out of PrefabPreviewWindow. Owns the gizmo operations, the per-panel
    // save timers, and the undo edit brackets; reaches camera/environment through the window (friend).
    class PrefabAuthoringPanels
    {
    public:
        explicit PrefabAuthoringPanels(PrefabRigEditContext& ctx, PrefabPreviewWindow& w)
            : ctx(ctx), w(w) {}

        void drawAuthoringPanel();
        void drawGizmos(); // dispatches to exactly one gizmo per gizmoMode
        // VK-1433 Phase 1b — when armed, hit-test the selected skeletal part's joints against the
        // viewport click and, on a hit, prefill + add a bone socket (with a Phase-2 undo entry).
        void tryBonePick(const ImVec2& viewportMin, const ImVec2& viewportSize);

        // Selected-part helpers (the window knows part metadata from rigDesc).
        bool partIsSkeletal(int part) const;
        const std::string& partMeshPath(int part) const;
        void pullEditSocketsForPart(int part);
        void pushEditSocketsForPart(int part);
        void pullEditChains();
        void pushEditChains();

        // Drive the authoring panels from a new selection (set selectedPart + pull its sockets).
        void selectPart(int part);

    private:
        PrefabRigEditContext& ctx;
        PrefabPreviewWindow& w;

        // Live composed world of a part (gizmo anchor); identity if unavailable.
        glm::mat4 partWorldLive(int part) const;
        void drawTransformGizmo(); // VK-1433 Phase 4c — part ENTITY transform (TRS), persisted by Save
        void drawSocketGizmo();    // static-socket offset (anchored at live part world)

        void drawGizmoModeToolbar(); // VK-1433 Transform / Bone Socket / Static Socket / IK
        void drawStatePicker();
        void drawTransformPanel();   // VK-1433 Phase 4c — TRS op toggle + numeric fields edit the entity
        void zeroSourceTranslationForPart(int part); // Phase 2: bake a socketed child's position to 0
        void drawFrameScrub();       // VK-1433 Prev/Next frame + normalized scrub slider
        void drawBoneSocketPanel();
        void drawStaticSocketPanel();
        void drawIKPanel();

        // Source entity's OWN local transform (position/rotation-euler-deg/scale) for a part, read live
        // via CQRS. Used by the Transform panel + the socket-attached-child translate-drop warning.
        services::TransformData partSourceLocalTransform(int part) const;

        ImGuizmo::OPERATION socketGizmoOp = ImGuizmo::TRANSLATE;
        ImGuizmo::OPERATION transformGizmoOp = ImGuizmo::TRANSLATE; // TRS toggle for the part gizmo

        // Editable-copy save feedback timers / flags.
        float socketSaveTimer = 0.0f;
        bool socketSaveSuccess = false;
        float ikSaveTimer = 0.0f;
        bool ikSaveSuccess = false;

        // State-transition blend duration (seconds). Was hard-coded 0.25f; now a State-tab slider.
        float stateBlendDuration = 0.25f; // == prefabrigedit kDefaultStateBlendSeconds

        // Undo edit brackets: capture a "before" snapshot while no edit session is active, push ONE undo
        // command when the session ends. socketEditActive / chainEditActive track an in-flight panel
        // edit; gizmoEditActive tracks an ImGuizmo (static-socket) drag.
        bool socketEditActive = false;
        bool chainEditActive = false;
        bool gizmoEditActive = false; // static-socket gizmo drag bracket
        prefabrigedit::PrefabRigEditSnapshot socketEditBefore;
        prefabrigedit::PrefabRigEditSnapshot chainEditBefore;
        prefabrigedit::PrefabRigEditSnapshot gizmoEditBefore;

        // VK-1433 Phase 4d — TRANSFORM gizmo drag bracket. The Transform gizmo edits the part's source
        // ENTITY transform every Manipulate frame (live preview); we snapshot that entity transform on
        // the IsUsing() rising edge and push ONE coalesced entity-transform undo on release.
        bool transformGizmoEditActive = false;
        services::TransformData transformGizmoBefore; // entity local transform at drag start
    };
}
