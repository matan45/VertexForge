#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "AnimationInfoPanel.hpp"
#include "AnimationViewport.hpp"
#include "AnimationTimelinePanel.hpp"
#include "AnimationSkeletonPanel.hpp"
#include "AnimationPhysicsPanel.hpp"
#include "AnimationSocketPanel.hpp"
#include "AnimationIKChainPanel.hpp"
#include "resource/Types.hpp"
#include "providers/PreviewInstanceId.hpp"
#include "providers/animation/IAnimationPreviewProvider.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include <string>
#include <vector>
#include <future>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace editor { class OrbitCamera; }

namespace windows
{
    class AnimationPreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        explicit AnimationPreviewWindow(const std::string& filePath);
        ~AnimationPreviewWindow() override;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

    private:
        void startAsyncLoad();
        void updateAsyncLoading();
        void initPreviewRenderer();
        void cleanUpPreviewRenderer();
        void loadAnimationForPreview();
        void updateBoneTransformsFromService();
        void buildBoneHierarchyMaps();
        void updatePlayback(float deltaTime);

        services::PreviewInstanceId getPreviewInstanceId() const
        {
            return services::PreviewInstanceId(const_cast<AnimationPreviewWindow*>(this));
        }

    private:
        std::string animationPath;
        std::string windowTitle;

        resource::AnimationData animationData;
        std::future<std::shared_ptr<resource::AnimationData>> loadFuture;
        std::atomic<bool> loadingInProgress{false};
        std::string loadingStatus = "Starting...";

        std::unique_ptr<editor::OrbitCamera> camera;
        bool previewInitialized = false;
        bool previewCleanedUp = false;
        bool isDraggingPreview = false;

        float lastFrameTime = 0.0f;
        int currentFrame = 0;
        int firstFrame = 0;
        bool sequencerExpanded = true;
        int selectedChannel = -1;

        std::vector<services::EvaluatedBoneInfo> evaluatedBones;
        std::unordered_map<std::string, size_t> boneNameToIndex;
        std::unordered_map<int32_t, std::vector<size_t>> boneChildrenMap;

        bool isOpen = true;
        bool needsInit = true;
        bool showBoneVisualization = true;

        animation::AnimationInfoPanel::State panelState;
        animation::AnimationInfoPanel infoPanel;
        animation::AnimationViewport viewport;
        animation::AnimationTimelinePanel timelinePanel;
        animation::AnimationSkeletonPanel skeletonPanel;
        animation::AnimationPhysicsPanel physicsPanel;
        animation::AnimationSocketPanel socketPanel;

        types::PhysicsAnimationConfig physicsConfig;
        bool showPhysicsPanel = false;
        bool showSocketPanel = false;
        bool showColliderOverlay = true;
        bool showSocketVisualization = true;
        std::string physicsConfigPath;

        std::vector<animator::SocketDefinition> socketDefinitions;
        std::vector<animator::AnimationEvent> animationEvents;
        std::string lastLoadedMeshPath;  // Track mesh changes to reload sockets
        void loadSocketsFromMesh();

        animation::AnimationIKChainPanel ikChainPanel;
        std::vector<animator::ik::IKChainConfig> ikChainConfigs;
        bool showIKChainPanel = false;
        bool showIKChainVisualization = true;
        void loadIKChainsFromMesh();

        void buildMappedBoneNames();
        std::unordered_set<std::string> mappedBoneNames;
    };
}
