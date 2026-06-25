#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "PreviewWindowChrome.hpp"
#include "PreviewEnvironment.hpp"
#include "PrefabRigEditUndo.hpp"    // PrefabRigEditSnapshot + undo command (header-only)
#include "PrefabRigValidation.hpp"  // validatePartRefs + translate-drop detection (header-only)
#include "PrefabRigBonePick.hpp"    // Phase 1b screen-space bone-pick math (header-only)
#include "PrefabRigLiveDescBuilder.hpp" // VK-1433 Phase 4 live entity-tree -> DTO (header-only)
#include "PrefabRigEditContext.hpp"  // VK-1443 shared edit-state view + hoisted GizmoMode
#include "PrefabSandboxController.hpp"        // VK-1443 sandbox lifecycle + preview renderer
#include "PrefabHierarchyPanel.hpp"           // VK-1443 hierarchy tree + inspector + selection
#include "PrefabAuthoringPanels.hpp"          // VK-1443 viewport gizmos + authoring panels
#include "PrefabRigEditUndoCoordinator.hpp"   // VK-1443 undo snapshot/push coordinator
#include "nfd/FileDialog.hpp"        // VK-1433 Phase 4c Save-As dialog
#include "data/PrefabRigDescDTO.hpp"
#include "data/EntityHandle.hpp"
#include "data/DTOs.hpp"             // services::TransformData
#include "providers/PreviewInstanceId.hpp"
#include "events/EventTypes.hpp"     // events::SubscriptionToken (transform-change subscription)
#include "animator/SocketTypes.hpp"
#include "animator/IKTypes.hpp"
#include "ImGuizmo.h"
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <cstdint>
#include <optional>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace editor { class OrbitCamera; }

namespace windows
{
    class PrefabPreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
        // VK-1443 — the four sub-controllers reach window-private non-ctx state (camera, environment,
        // prefabPath, fileDialog, the undo statics, and the other controllers) through friend access.
        friend class PrefabSandboxController;
        friend class PrefabHierarchyPanel;
        friend class PrefabAuthoringPanels;
        friend class PrefabRigEditUndoCoordinator;

    private:
        std::string prefabPath;
        std::string windowTitle;

        // Loaded prefab metadata (read from the .vfPrefab JSON header once on open; the entity
        // hierarchy itself is now the LoadPrefab'd sandbox subtree, not a parsed JSON tree).
        std::string prefabName;
        std::string prefabVersion;
        uint32_t sandboxEntityCount = 0; // entities in the (visible) sandbox subtree, for the info panel
        std::string errorMessage;
        bool loadFailed = false;
        bool prefabLoaded = false;

        // Window state
        bool isOpen = true;
        bool needsInit = true;

        editor::preview::WindowMaximizer maximizer;
        ImVec2 initialSize{0.0f, 0.0f};
        bool sizeSaved = false;

        // VK-1433 Phase 4 — the LoadPrefab'd, PreviewSandboxTagComponent-tagged sandbox subtree. The
        // window never includes EntityRegistry; it owns these entities only through the scene CQRS
        // (LoadPrefab / Delete / Get / Select), exactly like UILayerBuilderWindow's canvasRoot.
        services::EntityHandle sandboxRoot = services::EntityHandle::invalid();
        // Source entity of each rig part, parallel to rigDesc.parts (so a selected entity maps to a
        // part and back). Re-derived with rigDesc on every structural edit.
        std::vector<services::EntityHandle> partEntities;

        // --- VK-1433 rig preview (viewport + authoring) ---------------------
        services::PrefabRigDescDTO rigDesc;          // re-derived from the sandbox subtree
        std::unique_ptr<editor::OrbitCamera> camera;
        editor::preview::PreviewEnvironment environment;

        bool previewInitialized = false;             // controller init command sent
        bool previewBuilt = false;                   // buildFromDesc succeeded
        bool isDraggingOrbit = false;
        bool isDraggingPan = false;
        float lastFrameTime = 0.0f;

        glm::quat previewRotation{1.0f, 0.0f, 0.0f, 0.0f}; // turntable

        // Authoring selection / mode.
        int selectedPart = -1;        // part index for the socket panels
        int selectedSocketIndex = -1; // socket index within the selected part
        int selectedChainIndex = -1;  // chain index for the IK panel

        // VK-1433 — exactly one gizmo is active at a time so they never fight. Transform drives the
        // part preview transform (editor-transient); StaticSocket edits a static part's own socket
        // offsets. Bone/IK have no direct gizmo (they ride bones / are panel-driven) — selecting
        // them simply suppresses the viewport gizmo.
        // GizmoMode is hoisted to namespace windows (PrefabRigEditContext.hpp) so the context and the
        // authoring sub-controller can name it too (a nested type would force a circular include).
        GizmoMode gizmoMode = GizmoMode::Transform;

        // VK-1433 Phase 1b — bone-pick arm flag. Set from the Bone Socket panel; while armed, a
        // viewport left-click (not consumed by ImGuizmo) hit-tests the selected skeletal part's joints
        // and prefills a new bone socket. Disarmed after one successful pick (or on part change).
        bool bonePickArmed = false;

        // Editable copies pulled from the controller, pushed back on change.
        std::vector<animator::SocketDefinition> editSockets;  // for selectedPart
        std::vector<animator::ik::IKChainConfig> editChains;
        bool chainsLoaded = false;                            // editChains pulled once

        // --- Phase 2 broken-ref validation -----------------------------------
        // Per-part broken-asset-reference report, recomputed once each time the rig description is
        // built (cheap fs::exists() over the parts; not per-frame). Parallel to rigDesc.parts.
        std::vector<prefabrigval::PartRefStatus> partRefStatuses;
        int missingRefCount = 0;

        // --- VK-1433 Phase 4c prefab save -------------------------------------
        // SavePrefabCommand on the (tagged) sandbox root: the serializer strips the preview tag and
        // normalizes isActive=true (PrefabSerialization), so the saved .vfPrefab round-trips clean.
        // dirty drives an "unsaved" badge; mirrors UILayerBuilderWindow::saveLayer.
        bool dirty = false;
        float prefabSaveTimer = 0.0f;
        bool prefabSaveSuccess = false;
        void savePrefab(bool saveAs);
        nfd::FileDialog fileDialog;

        // --- Phase 2 edit undo (VK-1443) --------------------------------------
        // Rebuild-by-id hook for the entity-transform undo command (analogue of resyncMirror): looks
        // up the live window by id and re-derives its rig, or no-ops if it has been closed. Stays on the
        // window (lowest-risk replay path); the snapshot/push helpers moved to PrefabRigEditUndoCoordinator.
        static void resyncRebuild(services::PreviewInstanceId id);

        // Mirror re-sync hook for the undo command. The command captures ONLY the id (by value) and
        // calls this static after replaying the controller CQRS. It looks up the live window by id
        // (nullptr if the window was closed → no-op, no dangling deref) and copies the restored
        // snapshot data back into that window's editSockets / editChains / previewTransforms mirror.
        // Mirrors the controller adapter's getController(id)->nullptr-guard lifetime pattern.
        static void resyncMirror(services::PreviewInstanceId id,
                                 const prefabrigedit::PrefabRigEditSnapshot& snap);
        void applyMirrorSnapshot(const prefabrigedit::PrefabRigEditSnapshot& snap); // instance helper

        services::PreviewInstanceId getInstanceId() const
        {
            return services::PreviewInstanceId(const_cast<PrefabPreviewWindow*>(this));
        }

        // Process-global registry of live PrefabPreviewWindows, keyed by PreviewInstanceId value
        // (== `this`). Registered in the ctor, erased in the dtor BEFORE the object is freed, so an
        // undo command that outlives the window resolves to nullptr instead of a dangling pointer.
        static std::unordered_map<std::uintptr_t, PrefabPreviewWindow*>& liveWindows();

    public:
        explicit PrefabPreviewWindow(const std::string& filePath);
        ~PrefabPreviewWindow() override;

        void draw() override;

        bool shouldClose() const override { return !isOpen; }
        const std::string& getPrefabPath() const { return prefabPath; }

    private:
        // VK-1443 — the bulk of the panels/lifecycle/authoring/undo were split into four
        // sub-controllers (see the friend decls + members below). The window keeps only draw(),
        // the viewport (which calls into the authoring controller for gizmos/picks), the info panel,
        // savePrefab, and the undo live-window registry + replay statics.
        void drawViewport(float regionWidth, float regionHeight, float deltaTime);
        void drawInfoPanel();

        // VK-1433 Phase 4 — window-local selection STORAGE (referenced by ctx.selectedSandboxEntity;
        // read/written through the hierarchy controller's selectedEntity()/selectEntity()). Routing it
        // through the global scene selection would leak a sandbox entity into the main Scene Hierarchy /
        // Details panels (and a main-scene selection back into here, blanking this inspector).
        services::EntityHandle selectedSandboxEntity_ = services::EntityHandle::invalid();

        // VK-1443 — shared edit-state view bound to the fields above (storage stays in this window).
        // Declared after the fields so its reference members bind to already-declared fields; each of
        // the four sub-controllers takes a PrefabRigEditContext& to reach this shared state. instanceId
        // is a value copy of getInstanceId() (== this), set in the ctor init list.
        PrefabRigEditContext ctx;

        // VK-1443 — the four sub-controllers. Declared AFTER ctx so each binds to a fully-initialized
        // context (and *this). Each holds `PrefabRigEditContext& ctx` + `PrefabPreviewWindow& w`.
        PrefabSandboxController sandboxController;
        PrefabHierarchyPanel hierarchyPanel;
        PrefabAuthoringPanels authoringController;
        PrefabRigEditUndoCoordinator undoCoordinator;
    };
}
