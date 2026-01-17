#pragma once

#include <glm/glm.hpp>
#include <math/Frustum.hpp>
#include "../animation/AnimationEvaluator.hpp"
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
    class AnimatedMeshPreviewController
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        std::unique_ptr<core::CommandPool> commandPool;

        vk::Sampler sampler;
        std::unique_ptr<core::OffscreenResources> offscreenResources;
        std::vector<vk::Fence> inFlightFences;

        std::unique_ptr<render::mesh::SkinnedMeshPipeline> skinnedPipeline;

        animation::AnimationEvaluator animEvaluator;
        resource::AnimationData animationData;
        resource::SkeletonData skeletonData;
        bool animationLoaded = false;
        bool meshLoaded = false;

        render::mesh::SkinnedMeshRenderData renderData;
        render::mesh::AnimationPlaybackState playbackState;

        glm::mat4 currentView{1.0f};
        glm::mat4 currentProjection{1.0f};
        glm::vec3 currentCameraPos{0.0f};

        bool initialized = false;
        std::string loadedMeshPath;
        std::string loadedAnimationPath;
        math::AABB meshBounds;

        std::vector<int32_t> meshToAnimBoneMapping;

    public:
        explicit AnimatedMeshPreviewController();
        ~AnimatedMeshPreviewController();

        AnimatedMeshPreviewController(const AnimatedMeshPreviewController&) = delete;
        AnimatedMeshPreviewController& operator=(const AnimatedMeshPreviewController&) = delete;

        void init();
        void cleanUp();

        bool loadMesh(const std::string& meshPath);
        bool loadAnimation(const std::string& animationPath);
        void unload();
        const std::string& getLoadedMeshPath() const { return loadedMeshPath; }
        bool isMeshLoaded() const { return meshLoaded; }

        void play() { playbackState.play(); }
        void pause() { playbackState.pause(); }
        void stop() { playbackState.stop(); }
        bool isPlaying() const { return playbackState.isPlaying; }

        void setPlaybackTime(float timeSeconds);
        float getPlaybackTime() const { return playbackState.currentTime; }
        void setLooping(bool loop) { playbackState.looping = loop; }
        void setPlaybackSpeed(float speed) { playbackState.playbackSpeed = speed; }

        void update(float deltaTime);

        void updateCamera(const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& cameraPos);

        void setModelMatrix(const glm::mat4& matrix) { renderData.modelMatrix = matrix; }

        void setAlbedo(const glm::vec4& color) { renderData.albedo = color; }
        void setMetallic(float value) { renderData.metallic = value; }
        void setRoughness(float value) { renderData.roughness = value; }

        void* render();

        const std::vector<animation::EvaluatedBone>& getEvaluatedBones() const { return animEvaluator.getEvaluatedBones(); }
        const std::vector<resource::SkeletonBone>& getSkeleton() const { return skeletonData.bones; }

    private:
        void createOffscreenResources();
        void cleanupOffscreenResources();
        void createSampler();
        void updateDescriptorSet(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;

        void buildBoneMapping();
        std::vector<glm::mat4> remapBoneMatrices(const std::vector<glm::mat4>& animBoneMatrices) const;
    };
}
