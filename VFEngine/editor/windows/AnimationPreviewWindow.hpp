#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "resource/Types.hpp"
#include "providers/PreviewInstanceId.hpp"
#include "providers/IAnimationPreviewProvider.hpp"
#include <string>
#include <vector>
#include <future>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>
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
    class AnimationPreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string animationPath;
        std::string windowTitle;

        resource::AnimationData animationData;
        bool loadFailed = false;
        bool animationLoaded = false;

        std::future<std::shared_ptr<resource::AnimationData>> loadFuture;
        std::atomic<bool> loadingInProgress{false};
        std::string loadingStatus = "Starting...";

        std::unique_ptr<editor::OrbitCamera> camera;
        bool previewInitialized = false;
        bool meshLoadedInPreview = false;
        bool animationLoadedInPreview = false;
        bool previewCleanedUp = false;
        bool isDraggingPreview = false;

        bool isPlaying = false;
        bool isLooping = true;
        float playbackSpeed = 1.0f;
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

        class AnimationSequence;
        std::unique_ptr<AnimationSequence> sequenceAdapter;

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

        void tryAutoLoadMesh();
        void loadAnimationForPreview();

        void updateBoneTransformsFromService();
        void buildBoneHierarchyMaps();

        void drawInfoPanel();
        void draw3DViewport(float width, float height);
        void drawTimelinePanel();
        void drawSkeletonPanel();
        void drawBoneNode(size_t index);
        void drawPlaybackControls();
        void drawLoadingIndicator();
        void drawMeshFileInput();

        void handlePreviewInput();

        void drawBoneVisualization(const ImVec2& viewportPos, const ImVec2& viewportSize);
        ImVec2 worldToScreen(const glm::vec3& worldPos, const ImVec2& viewportPos, const ImVec2& viewportSize) const;
        bool showBoneVisualization = true;

        void updatePlayback(float deltaTime);
        void seekToTime(float timeInTicks);
        int timeToFrame(float timeInTicks) const;
        float frameToTime(int frame) const;

        services::PreviewInstanceId getPreviewInstanceId() const
        {
            return services::PreviewInstanceId(const_cast<AnimationPreviewWindow*>(this));
        }
    };
}
