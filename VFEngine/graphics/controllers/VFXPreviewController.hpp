#pragma once

#include "../render/vfx/VFXBillboardTypes.hpp"
#include "../core/OffScreen.hpp"
#include <vfx/VFXModifierTypes.hpp>
#include <vfx/VFXForceTypes.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace core
{
    class Device;
    class SwapChain;
    class CommandPool;
}

namespace render::vfx
{
    class VFXBillboardPipeline;
    class VFXParticleSystem;
}

namespace controllers
{
    struct VFXPreviewParams
    {
        float spawnRate = 10.0f;
        float lifetime = 2.0f;
        float startSize = 1.0f;
        float startSpeed = 1.0f;
        glm::vec4 startColor{1.0f, 1.0f, 1.0f, 1.0f};
        glm::vec3 emitDirection{0.0f, 1.0f, 0.0f};
        std::string texturePath;

        // Modifier chain (VK-238)
        ::vfx::VFXModifierChain modifiers;

        // Force chain (VK-239)
        ::vfx::VFXForceChain forces;
    };

    class VFXPreviewController
    {
    private:
        core::SwapChain& swapChain;
        core::Device& device;
        std::unique_ptr<core::CommandPool> commandPool;
        std::unique_ptr<render::vfx::VFXBillboardPipeline> pipeline;
        std::unique_ptr<render::vfx::VFXParticleSystem> particleSystem;

        core::OffscreenResources offscreenResources;
        vk::Sampler sampler;
        std::vector<vk::Fence> inFlightFences;

        VFXPreviewParams currentParams;
        bool initialized = false;

    public:
        explicit VFXPreviewController();
        ~VFXPreviewController();

        void init();
        void cleanUp();

        void setParams(const VFXPreviewParams& params);
        const VFXPreviewParams& getParams() const { return currentParams; }

        void updateCamera(const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& cameraPos, float time);
        void update(float deltaTime);

        void play();
        void pause();
        void stop();
        bool isPlaying() const;

        void* render();
        bool isInitialized() const { return initialized; }

    private:
        void createOffscreenResources();
        void cleanupOffscreenResources();
        void createSampler();
        void updateDescriptorSets(vk::DescriptorSet& descriptorSet, const vk::ImageView& imageView) const;
    };
}
