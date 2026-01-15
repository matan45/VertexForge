#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "resource/Types.hpp"
#include "providers/PreviewInstanceId.hpp"
#include "providers/IAnimationPreviewProvider.hpp"
#include <math/Frustum.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <future>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

namespace ImSequencer
{
    struct SequenceInterface;
}

namespace editor
{
    class OrbitCamera;
}

namespace windows
{
    struct AnimationLoadResult
    {
        bool success = false;
        std::string errorMessage;
        resource::AnimationData animationData;
    };

    class AnimationPreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        // File paths
        std::string animationPath;
        std::string windowTitle;

        // Loaded animation data (for timeline display and bone hierarchy)
        resource::AnimationData animationData;
        bool loadFailed = false;
        bool animationLoaded = false;

        // Async loading for animation data
        std::future<AnimationLoadResult> loadFuture;
        std::atomic<bool> loadingInProgress{false};
        std::atomic<bool> loadingCancelled{false};
        std::string loadingStatus = "Starting...";

        // 3D Preview state
        std::unique_ptr<editor::OrbitCamera> camera;
        math::AABB meshBounds;
        bool previewInitialized = false;
        bool meshLoadedInPreview = false;
        bool animationLoadedInPreview = false;
        bool previewCleanedUp = false;
        bool isDraggingPreview = false;

        // Playback state (synced with service)
        bool isPlaying = false;
        bool isLooping = true;
        float playbackSpeed = 1.0f;
        float lastFrameTime = 0.0f;

        // ImSequencer state
        int currentFrame = 0;
        int firstFrame = 0;
        bool sequencerExpanded = true;
        int selectedChannel = -1;

        // Evaluated skeleton (from service for display)
        std::vector<services::EvaluatedBoneInfo> evaluatedBones;
        std::unordered_map<std::string, size_t> boneNameToIndex;
        std::unordered_map<int32_t, std::vector<size_t>> boneChildrenMap;

        // Window state
        bool isOpen = true;
        bool needsInit = true;

        // ImSequencer adapter
        class AnimationSequence;
        std::unique_ptr<AnimationSequence> sequenceAdapter;

    public:
        explicit AnimationPreviewWindow(const std::string& filePath);
        ~AnimationPreviewWindow() override;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }
        const std::string& getAnimationPath() const { return animationPath; }

    private:
        // Initialization and loading
        void startAsyncLoad();
        void updateAsyncLoading();
        AnimationLoadResult loadAnimationBackground(const std::string& path);
        void initPreviewRenderer();
        void cleanUpPreviewRenderer();

        // Mesh loading for 3D preview
        void tryAutoLoadMesh();
        void loadAnimationForPreview();

        // Get bone transforms from the service (same data used for GPU skinning)
        void updateBoneTransformsFromService();
        void buildBoneHierarchyMaps();

        // Drawing methods
        void drawInfoPanel();
        void draw3DViewport(float width, float height);
        void drawTimelinePanel();
        void drawSkeletonPanel();
        void drawBoneNode(size_t index);
        void drawPlaybackControls();
        void drawLoadingIndicator();
        void drawMeshFileInput();

        // 3D viewport input
        void handlePreviewInput();

        // Debug visualization
        void drawBoneVisualization(const ImVec2& viewportPos, const ImVec2& viewportSize);
        ImVec2 worldToScreen(const glm::vec3& worldPos, const ImVec2& viewportPos, const ImVec2& viewportSize) const;
        bool showBoneVisualization = true;

        // Playback
        void updatePlayback(float deltaTime);
        void seekToTime(float timeInTicks);
        int timeToFrame(float timeInTicks) const;
        float frameToTime(int frame) const;

        // Preview instance ID
        services::PreviewInstanceId getPreviewInstanceId() const { return services::PreviewInstanceId(const_cast<AnimationPreviewWindow*>(this)); }
    };
}
