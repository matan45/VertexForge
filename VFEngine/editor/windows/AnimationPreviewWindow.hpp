#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "resource/Types.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <future>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace ImSequencer
{
    struct SequenceInterface;
}

namespace windows
{
    struct AnimationLoadResult
    {
        bool success = false;
        std::string errorMessage;
        resource::AnimationData animationData;
    };

    struct EvaluatedBoneTransform
    {
        std::string boneName;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
        glm::mat4 localTransform{1.0f};
        glm::mat4 worldTransform{1.0f};
        int32_t parentIndex = -1;
    };

    class AnimationPreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        // File info
        std::string animationPath;
        std::string windowTitle;

        // Loaded animation data
        resource::AnimationData animationData;
        bool loadFailed = false;
        bool animationLoaded = false;

        // Async loading
        std::future<AnimationLoadResult> loadFuture;
        std::atomic<bool> loadingInProgress{false};
        std::atomic<bool> loadingCancelled{false};
        std::string loadingStatus = "Starting...";

        // Playback state
        bool isPlaying = false;
        bool isLooping = true;
        float currentTime = 0.0f;
        float playbackSpeed = 1.0f;
        float lastFrameTime = 0.0f;

        // ImSequencer state
        int currentFrame = 0;
        int firstFrame = 0;
        bool sequencerExpanded = true;
        int selectedChannel = -1;

        // Evaluated skeleton
        std::vector<EvaluatedBoneTransform> evaluatedBones;
        std::unordered_map<std::string_view, size_t> boneNameToChannelIndex;
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

        // Animation evaluation
        void evaluateAnimation(float timeInTicks);
        glm::vec3 interpolatePosition(const resource::BoneAnimation& channel, float time);
        glm::quat interpolateRotation(const resource::BoneAnimation& channel, float time);
        glm::vec3 interpolateScale(const resource::BoneAnimation& channel, float time);
        void computeWorldTransforms();

        // Drawing methods
        void drawInfoPanel();
        void drawMeshPreviewPlaceholder();
        void drawTimelinePanel();
        void drawSkeletonPanel();
        void drawBoneNode(size_t index);
        void drawPlaybackControls();
        void drawLoadingIndicator();

        // Playback
        void updatePlayback(float deltaTime);
        void seekToTime(float timeInTicks);
        int timeToFrame(float timeInTicks) const;
        float frameToTime(int frame) const;
    };
}
