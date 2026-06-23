#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "PreviewWindowChrome.hpp"
#include "PreviewEnvironment.hpp"
#include "PrefabRigDescBuilder.hpp" // PrefabEntityNode + buildPrefabRigDescDTO (header-only)
#include "data/PrefabRigDescDTO.hpp"
#include "providers/PreviewInstanceId.hpp"
#include "animator/SocketTypes.hpp"
#include "animator/IKTypes.hpp"
#include "ImGuizmo.h"
#include <string>
#include <vector>
#include <map>
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

        // VK-1433 "Save Transforms to Prefab": bake the gizmo previewTransforms into the .vfPrefab
        // JSON's per-entity transforms (entt-free, direct JSON round-trip). Root is skipped by
        // default (its gizmo = whole-rig framing); the user can opt in.
        float transformSaveTimer = 0.0f;
        bool transformSaveSuccess = false;
        int transformSaveCount = 0;
        bool includeRootInSave = false;

        services::PreviewInstanceId getInstanceId() const
        {
            return services::PreviewInstanceId(const_cast<PrefabPreviewWindow*>(this));
        }

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
        void drawEntityNode(const PrefabEntityNode& node, const std::string& path);
        void drawLoadingIndicator();
        void drawAuthoringPanel();
        void drawGizmoModeToolbar(); // VK-1433 Transform / Bone Socket / Static Socket / IK
        void drawStatePicker();
        void drawTransformPanel();   // VK-1433 TRS op toggle + Reset Transform + Save to Prefab
        void saveTransformsToPrefab(); // VK-1433 bake previewTransforms into the .vfPrefab JSON
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
