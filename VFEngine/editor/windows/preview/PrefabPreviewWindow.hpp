#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "PreviewWindowChrome.hpp"
#include "PreviewEnvironment.hpp"
#include "PrefabRigDescBuilder.hpp" // PrefabEntityNode + buildPrefabRigDescDTO (header-only)
#include "PrefabRigEditUndo.hpp"    // PrefabRigEditSnapshot + undo command (header-only)
#include "PrefabRigValidation.hpp"  // validatePartRefs + translate-drop detection (header-only)
#include "data/PrefabRigDescDTO.hpp"
#include "providers/PreviewInstanceId.hpp"
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
#include <future>
#include <atomic>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace editor { class OrbitCamera; }

namespace windows
{
    struct ComponentStats
    {
        std::map<std::string, uint32_t> counts;
        uint32_t totalEntities = 0;
    };

    struct PrefabLoadResult
    {
        bool success = false;
        std::string errorMessage;
        std::string prefabName;
        std::string version;
        PrefabEntityNode rootEntity;
        ComponentStats stats;
    };

    class PrefabPreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string prefabPath;
        std::string windowTitle;

        // Loaded prefab data
        std::string prefabName;
        std::string prefabVersion;
        PrefabEntityNode rootEntity;
        ComponentStats componentStats;
        std::string errorMessage;
        bool loadFailed = false;
        bool prefabLoaded = false;

        // Window state
        bool isOpen = true;
        bool needsInit = true;

        editor::preview::WindowMaximizer maximizer;
        ImVec2 initialSize{0.0f, 0.0f};
        bool sizeSaved = false;

        // Async loading state
        std::future<PrefabLoadResult> loadFuture;
        std::atomic<bool> loadingInProgress{false};
        std::atomic<bool> loadingCancelled{false};
        std::string loadingStatus = "Loading prefab...";

        // Tree view state
        std::optional<std::string> selectedEntityPath;

        // --- VK-1433 rig preview (viewport + authoring) ---------------------
        services::PrefabRigDescDTO rigDesc;          // built once the async parse completes
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

        // Window-authoritative editor-transient preview transforms (part -> matrix). The assembly
        // also holds these (it folds them into partWorld), but the window keeps its own copy so it
        // can recover the gizmo's base anchor (world without the preview) each frame. Reset on
        // rebuild/close; NEVER serialized. Absent entry == identity.
        std::map<int, glm::mat4> previewTransforms;
        glm::mat4 currentPartPreviewTransform(int part) const;

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

        // VK-1433 "Save Transforms to Prefab": bake the gizmo previewTransforms into the .vfPrefab
        // JSON's per-entity transforms (entt-free, direct JSON round-trip). Root is skipped by
        // default (its gizmo = whole-rig framing); the user can opt in.
        float transformSaveTimer = 0.0f;
        bool transformSaveSuccess = false;
        int transformSaveCount = 0;
        bool includeRootInSave = false;

        // --- Phase 2 broken-ref validation -----------------------------------
        // Per-part broken-asset-reference report, recomputed once each time the rig description is
        // built (cheap fs::exists() over the parts; not per-frame). Parallel to rigDesc.parts.
        std::vector<prefabrigval::PartRefStatus> partRefStatuses;
        int missingRefCount = 0;
        void revalidateRefs(); // fills partRefStatuses + missingRefCount from rigDesc on disk

        // --- Phase 2 drag-drop part-ref swap (transient) ----------------------
        // Dropping a .vfMesh/.vfMaterial/.vfAnim onto the Part combo swaps that ref on rigDesc and
        // rebuilds the preview live (zero CQRS — the whole desc round-trips on rebuild). The swap is
        // editor-transient until the user saves: hasUnsavedRefSwap drives an "unsaved" badge. Disk
        // persistence is a separate explicit action (see PrefabRefWriter / report).
        bool hasUnsavedRefSwap = false;
        float refSaveTimer = 0.0f;
        bool refSaveSuccess = false;
        std::set<int> swappedParts; // parts whose refs were drag-swapped (only these are persisted)
        void applyAssetDropToPart(int part, const std::string& assetPath); // by extension; rebuilds
        void saveRefSwapsToPrefab(); // persist swapped part refs into the .vfPrefab JSON

        // --- Phase 2 edit undo ------------------------------------------------
        // Snapshot/push brackets mirror UILayerBuilderWindow: capture a "before" snapshot while no
        // edit session is active, push ONE undo command when the session ends. socketEditActive /
        // chainEditActive track an in-flight panel edit; gizmoEditActive tracks an ImGuizmo drag.
        bool socketEditActive = false;
        bool chainEditActive = false;
        bool gizmoEditActive = false;            // shared by the transform gizmo + static-socket gizmo
        bool transformPanelEditActive = false;   // numeric Transform fields / Reset buttons
        prefabrigedit::PrefabRigEditSnapshot socketEditBefore;
        prefabrigedit::PrefabRigEditSnapshot chainEditBefore;
        prefabrigedit::PrefabRigEditSnapshot gizmoEditBefore;
        prefabrigedit::PrefabRigEditSnapshot transformPanelBefore;

        // Build a snapshot of the data a given edit kind touches (engages only the relevant fields).
        prefabrigedit::PrefabRigEditSnapshot snapshotSockets() const;   // editSockets for selectedPart
        prefabrigedit::PrefabRigEditSnapshot snapshotChains() const;    // editChains + IK bindings
        prefabrigedit::PrefabRigEditSnapshot snapshotTransforms() const; // previewTransforms (whole map)
        std::vector<prefabrigedit::IKBindingSnapshot> snapshotIKBindings() const; // rigDesc.ik bindings

        // Push a single coalesced undo entry (no-op if before == after data). The pushed command
        // holds ONLY the PreviewInstanceId (a value) + snapshots — never a window pointer — so it is
        // safe to undo/redo after the window is closed (the command's CQRS replay no-ops on a dead
        // instanceId, and the mirror re-sync below no-ops when no live window matches the id).
        void pushSocketUndo(prefabrigedit::PrefabRigEditSnapshot before);
        void pushChainUndo(prefabrigedit::PrefabRigEditSnapshot before);
        void pushTransformUndo(prefabrigedit::PrefabRigEditSnapshot before);

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
        void startAsyncLoad();
        void updateAsyncLoading();
        PrefabLoadResult loadPrefabBackground(const std::string& path);

        // Rig preview lifecycle (service boundary; no Graphics/Core includes).
        void initPreviewRenderer();
        void buildPreviewFromDesc();
        void cleanUpPreviewRenderer();
        void drawViewport(float regionWidth, float regionHeight, float deltaTime);
        void drawGizmos();        // dispatches to exactly one gizmo per gizmoMode
        void drawTransformGizmo(); // VK-1433 part preview transform (TRS)
        void drawSocketGizmo();    // static-socket offset (anchored at live part world)

        // Live composed world of a part (gizmo anchor); identity if unavailable.
        glm::mat4 partWorldLive(int part) const;

        // Panels.
        void drawInfoPanel();
        void drawEntityTreePanel();
        // partCounter tracks DFS pre-order index of mesh-bearing nodes (== part index) so each
        // node can look up its broken-ref status; incremented for every mesh-bearing node visited.
        void drawEntityNode(const PrefabEntityNode& node, const std::string& path, int& partCounter);
        // Tooltip text listing a part's missing references (empty if none / out of range).
        std::string missingRefTooltip(int part) const;
        void drawLoadingIndicator();
        void drawAuthoringPanel();
        void drawGizmoModeToolbar(); // VK-1433 Transform / Bone Socket / Static Socket / IK
        void drawStatePicker();
        void drawTransformPanel();   // VK-1433 TRS op toggle + Reset Transform + Save to Prefab
        void saveTransformsToPrefab(); // VK-1433 bake previewTransforms into the .vfPrefab JSON
        void zeroSourceTranslationForPart(int part); // Phase 2: bake a socketed child's position to 0
        void drawFrameScrub();       // VK-1433 Prev/Next frame + normalized scrub slider
        void drawBoneSocketPanel();
        void drawStaticSocketPanel();
        void drawIKPanel();

        // Selected-part helpers (the window knows part metadata from rigDesc).
        bool partIsSkeletal(int part) const;
        const std::string& partMeshPath(int part) const;
        void pullEditSocketsForPart(int part);
        void pushEditSocketsForPart(int part);
        void pullEditChains();
        void pushEditChains();
    };
}
