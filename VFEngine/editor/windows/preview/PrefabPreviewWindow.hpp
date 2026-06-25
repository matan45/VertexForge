#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "PreviewWindowChrome.hpp"
#include "PreviewEnvironment.hpp"
#include "PrefabRigEditUndo.hpp"    // PrefabRigEditSnapshot + undo command (header-only)
#include "PrefabRigValidation.hpp"  // validatePartRefs + translate-drop detection (header-only)
#include "PrefabRigBonePick.hpp"    // Phase 1b screen-space bone-pick math (header-only)
#include "PrefabRigLiveDescBuilder.hpp" // VK-1433 Phase 4 live entity-tree -> DTO (header-only)
#include "../scene/EntityDetailsPanel.hpp" // VK-1433 Phase 4b embedded component inspector
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
#include <unordered_set>
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
        // EDITOR-ONLY, non-persistent preview-hide set (entity id). A node here (or any descendant)
        // is pruned from the re-derived DTO; isActive is NEVER touched (4b populates this via an
        // eye toggle — the builder consumes it).
        std::unordered_set<uint64_t> hiddenEntities;

        // VK-1433 Phase 4b — inline-rename state (clone of UILayerBuilderWindow). renamingEntity is the
        // node whose label is currently an InputText; renameFocusPending grabs keyboard focus once.
        services::EntityHandle renamingEntity = services::EntityHandle::invalid();
        bool renameFocusPending = false;
        char renameBuf[256] = {};

        // VK-1433 Phase 4b — embedded shared component inspector (O1). Hosting an EntityDetailsPanel
        // and calling its reusable drawComponentSection() gives the full per-component edit + Remove
        // stack and the Add Component popup for the selected sandbox entity, without re-listing every
        // drawer here. The window stays entt-free (the panel and its drawers are all CQRS-based).
        EntityDetailsPanel entityInspector;

        // --- VK-1433 rig preview (viewport + authoring) ---------------------
        services::PrefabRigDescDTO rigDesc;          // re-derived from the sandbox subtree
        std::unique_ptr<editor::OrbitCamera> camera;
        editor::preview::PreviewEnvironment environment;

        bool previewInitialized = false;             // controller init command sent
        bool previewBuilt = false;                   // buildFromDesc succeeded
        bool previewCleanedUp = false;
        bool isDraggingOrbit = false;
        bool isDraggingPan = false;
        float lastFrameTime = 0.0f;

        glm::quat previewRotation{1.0f, 0.0f, 0.0f, 0.0f}; // turntable

        // Authoring selection / mode.
        int selectedPart = -1;        // part index for the socket panels
        int selectedSocketIndex = -1; // socket index within the selected part
        int selectedChainIndex = -1;  // chain index for the IK panel
        ImGuizmo::OPERATION socketGizmoOp = ImGuizmo::TRANSLATE;

        // VK-1433 — exactly one gizmo is active at a time so they never fight. Transform drives the
        // part preview transform (editor-transient); StaticSocket edits a static part's own socket
        // offsets. Bone/IK have no direct gizmo (they ride bones / are panel-driven) — selecting
        // them simply suppresses the viewport gizmo.
        enum class GizmoMode { Transform, BoneSocket, StaticSocket, IK };
        GizmoMode gizmoMode = GizmoMode::Transform;
        ImGuizmo::OPERATION transformGizmoOp = ImGuizmo::TRANSLATE; // TRS toggle for the part gizmo

        // VK-1433 Phase 1b — bone-pick arm flag. Set from the Bone Socket panel; while armed, a
        // viewport left-click (not consumed by ImGuizmo) hit-tests the selected skeletal part's joints
        // and prefills a new bone socket. Disarmed after one successful pick (or on part change).
        bool bonePickArmed = false;

        // Editable copies pulled from the controller, pushed back on change.
        std::vector<animator::SocketDefinition> editSockets;  // for selectedPart
        std::vector<animator::ik::IKChainConfig> editChains;
        bool chainsLoaded = false;                            // editChains pulled once
        float socketSaveTimer = 0.0f;
        bool socketSaveSuccess = false;
        float ikSaveTimer = 0.0f;
        bool ikSaveSuccess = false;

        // State-transition blend duration (seconds). Was hard-coded 0.25f; now a State-tab slider.
        float stateBlendDuration = 0.25f; // == prefabrigedit kDefaultStateBlendSeconds

        // --- Phase 2 broken-ref validation -----------------------------------
        // Per-part broken-asset-reference report, recomputed once each time the rig description is
        // built (cheap fs::exists() over the parts; not per-frame). Parallel to rigDesc.parts.
        std::vector<prefabrigval::PartRefStatus> partRefStatuses;
        int missingRefCount = 0;
        void revalidateRefs(); // fills partRefStatuses + missingRefCount from rigDesc on disk

        // --- VK-1433 Phase 4c drag-drop part-ref swap (persistent) ------------
        // Dropping a .vfMesh/.vfAnim onto the Part combo dispatches SetMeshDataCommand and a
        // .vfMaterial dispatches SetDefaultMaterialCommand on the part's SOURCE ENTITY, then
        // re-derives the rig. The entity is now the source of truth (SavePrefab persists it), so the
        // Phase-2 transient swappedParts / restore-after-rebuild guard is retired.
        void applyAssetDropToPart(int part, const std::string& assetPath); // by extension; rebuilds

        // --- VK-1433 Phase 4c prefab save -------------------------------------
        // SavePrefabCommand on the (tagged) sandbox root: the serializer strips the preview tag and
        // normalizes isActive=true (PrefabSerialization), so the saved .vfPrefab round-trips clean.
        // dirty drives an "unsaved" badge; mirrors UILayerBuilderWindow::saveLayer.
        bool dirty = false;
        float prefabSaveTimer = 0.0f;
        bool prefabSaveSuccess = false;
        void savePrefab(bool saveAs);
        nfd::FileDialog fileDialog;

        // --- Phase 2 edit undo ------------------------------------------------
        // Snapshot/push brackets mirror UILayerBuilderWindow: capture a "before" snapshot while no
        // edit session is active, push ONE undo command when the session ends. socketEditActive /
        // chainEditActive track an in-flight panel edit; gizmoEditActive tracks an ImGuizmo drag.
        bool socketEditActive = false;
        bool chainEditActive = false;
        bool gizmoEditActive = false;            // static-socket gizmo drag bracket
        prefabrigedit::PrefabRigEditSnapshot socketEditBefore;
        prefabrigedit::PrefabRigEditSnapshot chainEditBefore;
        prefabrigedit::PrefabRigEditSnapshot gizmoEditBefore;

        // VK-1433 Phase 4d — TRANSFORM gizmo drag bracket. The Transform gizmo edits the part's source
        // ENTITY transform every Manipulate frame (live preview); we snapshot that entity transform on
        // the IsUsing() rising edge and push ONE coalesced entity-transform undo on release.
        bool transformGizmoEditActive = false;
        services::TransformData transformGizmoBefore; // entity local transform at drag start
        // Push a PrefabRigEntityTransformUndoCommand for `entity` (no-op if before == after). The
        // command captures only the handle + transforms by value + a rebuild-by-id callback, so it is
        // safe on the process-global undo stack after the window closes (replay no-ops on a dead
        // entity; the rebuild callback no-ops when no live window matches the id).
        void pushTransformUndo(services::EntityHandle entity,
                               const services::TransformData& before,
                               const services::TransformData& after);
        // Rebuild-by-id hook for the entity-transform undo command (analogue of resyncMirror): looks
        // up the live window by id and re-derives its rig, or no-ops if it has been closed.
        static void resyncRebuild(services::PreviewInstanceId id);

        // Build a snapshot of the data a given edit kind touches (engages only the relevant fields).
        prefabrigedit::PrefabRigEditSnapshot snapshotSockets() const;   // editSockets for selectedPart
        prefabrigedit::PrefabRigEditSnapshot snapshotChains() const;    // editChains + IK bindings
        std::vector<prefabrigedit::IKBindingSnapshot> snapshotIKBindings() const; // rigDesc.ik bindings

        // Push a single coalesced undo entry (no-op if before == after data). The pushed command
        // holds ONLY the PreviewInstanceId (a value) + snapshots — never a window pointer — so it is
        // safe to undo/redo after the window is closed (the command's CQRS replay no-ops on a dead
        // instanceId, and the mirror re-sync below no-ops when no live window matches the id).
        void pushSocketUndo(prefabrigedit::PrefabRigEditSnapshot before);
        void pushChainUndo(prefabrigedit::PrefabRigEditSnapshot before);

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
        // VK-1433 Phase 4 — sandbox lifecycle (the JSON-parse spine is retired). openSandbox()
        // LoadPrefabs the prefab into an isolated tagged subtree and re-derives the rig from it;
        // closeSandbox() deletes that subtree + tears down the renderer. rebuildRigFromSandbox()
        // re-runs the live builder after every structural edit and rebuilds the preview.
        void openSandbox();
        void closeSandbox();
        void rebuildRigFromSandbox();
        void readPrefabHeader(); // prefabName / prefabVersion from the .vfPrefab JSON header only

        // Rig preview lifecycle (service boundary; no Graphics/Core includes).
        void initPreviewRenderer();
        void buildPreviewFromDesc();
        void cleanUpPreviewRenderer();
        void drawViewport(float regionWidth, float regionHeight, float deltaTime);
        void drawGizmos();        // dispatches to exactly one gizmo per gizmoMode
        void drawTransformGizmo(); // VK-1433 Phase 4c — part ENTITY transform (TRS), persisted by Save
        void drawSocketGizmo();    // static-socket offset (anchored at live part world)

        // VK-1433 Phase 1b — when armed, hit-test the selected skeletal part's joints against the
        // viewport click and, on a hit, prefill + add a bone socket (with a Phase-2 undo entry).
        // viewportMin/viewportSize are the rendered Image's screen rect (== ImGuizmo::SetRect args).
        void tryBonePick(const ImVec2& viewportMin, const ImVec2& viewportSize);

        // Live composed world of a part (gizmo anchor); identity if unavailable.
        glm::mat4 partWorldLive(int part) const;

        // Panels.
        void drawInfoPanel();
        // VK-1433 Phase 4 — the hierarchy is now a CQRS-recursive tree over the live sandbox subtree
        // (clone of UILayerBuilderWindow::drawHierarchyNode + SceneHierarchyPanel DnD/eye). 4b adds
        // add/remove/rename/reparent/reorder/hide; depth threads through for the reorder drop zones.
        void drawEntityTreePanel();
        void drawEntityNode(services::EntityHandle entity, int depth);
        void drawReorderDropZone(services::EntityHandle parent, size_t index); // between-siblings insert
        // VK-1433 Phase 4d — shared create/delete used by the Hierarchy header toolbar, the per-node
        // context menu, and the Delete-key shortcut (so all three follow the same select/dirty/rebuild
        // path). createChildEntity adds an "Entity" child under `parent` and selects it.
        services::EntityHandle createChildEntity(services::EntityHandle parent);
        // requestDeleteEntity stages a delete + opens the confirm modal (all three delete affordances
        // route through it); the actual DeleteEntityCommand + reselect/rebuild runs in deleteEntity
        // ONLY on confirm. deleteEntity returns false if entity is invalid / the sandbox root.
        void requestDeleteEntity(services::EntityHandle entity);
        bool deleteEntity(services::EntityHandle entity);
        void drawDeleteConfirmPopup();            // "Delete '<name>' and its children?" modal
        services::EntityHandle pendingDeleteEntity = services::EntityHandle::invalid();
        std::string pendingDeleteName;            // captured at request time for the modal message
        bool openDeleteConfirmPopup = false;      // set by requestDeleteEntity, consumed in draw()
        // O2: would renaming the entity currently named `oldName` orphan a socket attachment that
        // resolves to it by name? Used to WARN (non-destructive) before a rename.
        bool renameWouldOrphanAttachment(const std::string& oldName) const;
        // Embedded full component inspector for the selected sandbox entity (4b, O1). Reuses the
        // shared EntityDetailsPanel drawers (edit + per-header Remove) + Add Component popup; a
        // structural component change re-derives the rig.
        void drawEntityInspector();
        // Tooltip text listing a part's missing references (empty if none / out of range).
        std::string missingRefTooltip(int part) const;
        void drawAuthoringPanel();
        void drawGizmoModeToolbar(); // VK-1433 Transform / Bone Socket / Static Socket / IK
        void drawStatePicker();
        void drawTransformPanel();   // VK-1433 Phase 4c — TRS op toggle + numeric fields edit the entity
        void zeroSourceTranslationForPart(int part); // Phase 2: bake a socketed child's position to 0
        void drawFrameScrub();       // VK-1433 Prev/Next frame + normalized scrub slider
        void drawBoneSocketPanel();
        void drawStaticSocketPanel();
        void drawIKPanel();

        // VK-1433 Phase 4b — structure signature of the sandbox subtree (entity ids + their component
        // type lists, DFS). A change between frames (a component add/remove via the embedded inspector,
        // a create/delete) re-derives the rig. Cheap CQRS-only walk; computed once per frame.
        std::vector<uint64_t> sandboxStructureSignature() const;
        std::vector<uint64_t> lastStructureSignature;

        // Transform VALUE edits (embedded inspector, numeric fields, the gizmo, undo replay) do NOT
        // shift the structure signature, so they are caught via a TransformChangedNotification
        // subscription instead: the callback sets transformsDirty, and draw() applies a CHEAP
        // transform-only sync (syncTransformsFromSandbox — no mesh reload) at end-of-frame, unless a
        // full structural rebuild already ran that frame. This replaces the gizmo's old per-frame full
        // rebuild (which reloaded the whole rig from disk every drag frame) and makes inspector edits
        // actually move the mesh. Subscribed in openSandbox(), unsubscribed in closeSandbox() — never
        // the dtor (it can run after the EventDispatcher is gone at shutdown).
        bool transformsDirty = false;
        events::SubscriptionToken transformChangedToken;
        // Re-derive the rig DTO from the sandbox (cheap, CQRS-only — resolves mesh refs to PATHS, never
        // loads geometry) and push a transform-ONLY update to the preview controller (no waitIdle /
        // pipeline teardown / mesh reload). Falls back to a full rebuild if the structure drifted.
        void syncTransformsFromSandbox();

        // Selected-part helpers (the window knows part metadata from rigDesc).
        bool partIsSkeletal(int part) const;
        const std::string& partMeshPath(int part) const;
        void pullEditSocketsForPart(int part);
        void pushEditSocketsForPart(int part);
        void pullEditChains();
        void pushEditChains();

        // VK-1433 Phase 4 — selection link between the live hierarchy and the part-indexed authoring
        // panels. selectedEntity() / selectEntity() wrap the scene CQRS (clone of UILayerBuilderWindow).
        services::EntityHandle selectedEntity() const;
        void selectEntity(services::EntityHandle entity);
        // Part index whose source entity == `entity` (linear search over partEntities); -1 if none.
        int partForEntity(services::EntityHandle entity) const;
        // Drive the authoring panels from a new selection (set selectedPart + pull its sockets).
        void selectPart(int part);

        // Source entity's OWN local transform (position/rotation-euler-deg/scale) for a part, read
        // live via CQRS. Replaces the retired rootEntity reads in the Transform panel + the
        // socket-attached-child translate-drop warning (entt-free).
        services::TransformData partSourceLocalTransform(int part) const;
    };
}
