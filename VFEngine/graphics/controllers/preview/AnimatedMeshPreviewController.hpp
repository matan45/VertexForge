#pragma once

#include <glm/glm.hpp>
#include <math/Frustum.hpp>
#include "../../animation/AnimationEvaluator.hpp"
#include "../../animation/RetargetContext.hpp"
#include "retargeting/RetargetTypes.hpp"
#include "../../render/mesh/SkinnedMeshTypes.hpp"
#include "../../../services/providers/render/IMeshPreviewProvider.hpp"
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

namespace render
{
    class ClearColor;
}

namespace render::preview
{
    class PreviewBackgroundRenderer;
    class PreviewGridRenderer;
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
        std::unique_ptr<render::ClearColor> clearColor;
        std::unique_ptr<render::preview::PreviewBackgroundRenderer> previewBackground;
        std::unique_ptr<render::preview::PreviewGridRenderer> previewGrid;
        services::PreviewEnvironmentParams environmentParams;

        animation::AnimationEvaluator animEvaluator;
        resource::AnimationData animationData;
        resource::SkeletonData skeletonData;
        bool animationLoaded = false;
        bool meshLoaded = false;

        // VK-910 live retarget preview: when active, animationData is a SOURCE clip
        // retargeted onto skeletonData (the loaded mesh's target skeleton).
        animation::RetargetContext retargetContext;
        resource::SkeletonData sourceSkeletonData;
        bool retargetActive = false;

        render::mesh::SkinnedMeshRenderData renderData;
        render::mesh::AnimationPlaybackState playbackState;

        glm::mat4 currentView{1.0f};
        glm::mat4 currentProjection{1.0f};
        glm::vec3 currentCameraPos{0.0f};

        bool initialized = false;
        std::string loadedMeshPath;
        std::string loadedAnimationPath;
        math::AABB meshBounds;

    public:
        explicit AnimatedMeshPreviewController();
        ~AnimatedMeshPreviewController();

        AnimatedMeshPreviewController(const AnimatedMeshPreviewController&) = delete;
        AnimatedMeshPreviewController& operator=(const AnimatedMeshPreviewController&) = delete;

        void init();
        void cleanUp();

        bool loadMesh(const std::string& meshPath);
        bool loadAnimation(const std::string& animationPath);
        // Load a source clip retargeted onto the currently-loaded mesh's skeleton.
        bool loadRetargetedAnimation(const std::string& sourceAnimPath,
                                     const resource::SkeletonData& sourceSkeleton,
                                     const retargeting::HumanoidRigData& sourceRig,
                                     const retargeting::HumanoidRigData& targetRig,
                                     const retargeting::RetargetMapData& map);
        void unload();

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
        void setClearColor(const glm::vec4& color);
        void setEnvironment(const services::PreviewEnvironmentParams& params) { environmentParams = params; }

        void* render();

        const std::vector<animation::EvaluatedBone>& getEvaluatedBones() const { return animEvaluator.getEvaluatedBones(); }
        const std::vector<resource::SkeletonBone>& getSkeleton() const { return skeletonData.bones; }

    private:
        void createOffscreenResources();
        void cleanupOffscreenResources();
        void createSampler();
        void updateDescriptorSet(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;
    };
}
