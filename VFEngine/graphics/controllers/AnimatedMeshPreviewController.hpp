#pragma once

#include <glm/glm.hpp>
#include <math/Frustum.hpp>
#include "AnimationEvaluator.hpp"
#include "../render/mesh/SkinnedMeshTypes.hpp"
#include "resource/Types.hpp"
#include <memory>
#include <string>

namespace core
{
    class Device;
    class SwapChain;
    class CommandPool;
    struct OffscreenResources;
}

namespace render::mesh
{
    class SkinnedMeshPipeline;
}

namespace controllers
{
    // Loading progress for animation preview
    struct AnimationPreviewLoadingProgress
    {
        enum class State
        {
            Idle,
            LoadingMesh,
            LoadingAnimation,
            Complete,
            Failed
        };

        State state = State::Idle;
        float progress = 0.0f;
        std::string statusMessage;
        std::string errorMessage;

        bool isDone() const { return state == State::Complete || state == State::Failed; }
        bool isLoading() const { return state == State::LoadingMesh || state == State::LoadingAnimation; }
    };

    // Animation preview controller manages mesh + animation rendering
    class AnimatedMeshPreviewController
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        std::unique_ptr<core::CommandPool> commandPool;

        // Offscreen rendering resources
        vk::Sampler sampler;
        std::unique_ptr<core::OffscreenResources> offscreenResources;
        std::vector<vk::Fence> inFlightFences;

        // Skinned mesh pipeline
        std::unique_ptr<render::mesh::SkinnedMeshPipeline> skinnedPipeline;

        // Animation evaluation
        AnimationEvaluator animEvaluator;
        resource::AnimationData animationData;
        bool animationLoaded = false;

        // Render data
        render::mesh::SkinnedMeshRenderData renderData;
        render::mesh::AnimationPlaybackState playbackState;

        // Current camera state
        glm::mat4 currentView{1.0f};
        glm::mat4 currentProjection{1.0f};
        glm::vec3 currentCameraPos{0.0f};

        // State
        bool initialized = false;
        std::string loadedMeshPath;
        std::string loadedAnimationPath;
        math::AABB meshBounds;

        // Bone remapping: maps mesh bone index to animation bone index
        // Value of -1 means the mesh bone has no corresponding animation bone
        std::vector<int32_t> meshToAnimBoneMapping;

    public:
        AnimatedMeshPreviewController();
        ~AnimatedMeshPreviewController();

        // Non-copyable
        AnimatedMeshPreviewController(const AnimatedMeshPreviewController&) = delete;
        AnimatedMeshPreviewController& operator=(const AnimatedMeshPreviewController&) = delete;

        void init();
        void cleanUp();

        // Load mesh and animation files
        bool loadMesh(const std::string& meshPath);
        bool loadAnimation(const std::string& animationPath);
        void unload();

        // Animation playback control
        void play() { playbackState.play(); }
        void pause() { playbackState.pause(); }
        void stop() { playbackState.stop(); }
        void togglePlayPause() { playbackState.togglePlayPause(); }
        bool isPlaying() const { return playbackState.isPlaying; }

        void setPlaybackTime(float timeSeconds);
        float getPlaybackTime() const { return playbackState.currentTime; }
        float getDuration() const { return playbackState.duration; }
        void setLooping(bool loop) { playbackState.looping = loop; }
        bool isLooping() const { return playbackState.looping; }
        void setPlaybackSpeed(float speed) { playbackState.playbackSpeed = speed; }
        float getPlaybackSpeed() const { return playbackState.playbackSpeed; }

        // Update (called each frame for animation playback)
        void update(float deltaTime);

        // Camera
        void updateCamera(const glm::mat4& view, const glm::mat4& projection,
                         const glm::vec3& cameraPos);

        // Model transform
        void setModelMatrix(const glm::mat4& matrix) { renderData.modelMatrix = matrix; }
        const glm::mat4& getModelMatrix() const { return renderData.modelMatrix; }

        // Material properties
        void setAlbedo(const glm::vec4& color) { renderData.albedo = color; }
        void setMetallic(float value) { renderData.metallic = value; }
        void setRoughness(float value) { renderData.roughness = value; }

        // Render and return ImGui-compatible descriptor set
        void* render();

        // State queries
        bool isMeshLoaded() const { return !loadedMeshPath.empty(); }
        bool isAnimationLoaded() const { return animationLoaded; }
        const math::AABB& getMeshBounds() const { return meshBounds; }

        // Animation info
        size_t getBoneCount() const { return animEvaluator.getBoneCount(); }
        const std::vector<EvaluatedBone>& getEvaluatedBones() const { return animEvaluator.getEvaluatedBones(); }
        const std::vector<resource::SkeletonBone>& getAnimationSkeleton() const { return animationData.skeleton; }

    private:
        void createOffscreenResources();
        void cleanupOffscreenResources();
        void createSampler();
        void updateDescriptorSet(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;

        // Build bone mapping from mesh skeleton to animation skeleton (by bone name)
        void buildBoneMapping();

        // Remap bone matrices from animation order to mesh order
        std::vector<glm::mat4> remapBoneMatrices(const std::vector<glm::mat4>& animBoneMatrices) const;
    };
}
