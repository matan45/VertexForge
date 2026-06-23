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

        // Editable copies pulled from the controller, pushed back on change.
        std::vector<animator::SocketDefinition> editSockets;  // for selectedPart
        std::vector<animator::ik::IKChainConfig> editChains;
        bool chainsLoaded = false;                            // editChains pulled once
        float socketSaveTimer = 0.0f;
        bool socketSaveSuccess = false;
        float ikSaveTimer = 0.0f;
        bool ikSaveSuccess = false;

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
        void drawSocketGizmo();

        // Panels.
        void drawInfoPanel();
        void drawEntityTreePanel();
        void drawEntityNode(const PrefabEntityNode& node, const std::string& path);
        void drawLoadingIndicator();
        void drawAuthoringPanel();
        void drawStatePicker();
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
