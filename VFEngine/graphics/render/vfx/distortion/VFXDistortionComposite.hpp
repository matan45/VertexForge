#pragma once

#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
    class Shader;
}

namespace render::vfx
{
    class VFXDistortionComposite
    {
    public:
        explicit VFXDistortionComposite(core::Device& device, core::SwapChain& swapChain);
        ~VFXDistortionComposite();

        VFXDistortionComposite(const VFXDistortionComposite&) = delete;
        VFXDistortionComposite& operator=(const VFXDistortionComposite&) = delete;

        void init(vk::Format colorFormat, vk::DescriptorSetLayout compositeDescLayout);
        void recreate(vk::Format colorFormat, vk::DescriptorSetLayout compositeDescLayout);
        void cleanup();
        bool isInitialized() const { return initialized; }

        void record(vk::CommandBuffer cmd, vk::ImageView colorImageView,
                    vk::Extent2D extent, vk::DescriptorSet compositeDescSet) const;

    private:
        core::Device& device;
        core::SwapChain& swapChain;

        std::shared_ptr<core::Shader> compositeShader;
        vk::Pipeline pipeline;
        vk::PipelineLayout pipelineLayout;

        bool initialized = false;

        void loadShader();
        void createPipeline(vk::Format colorFormat, vk::DescriptorSetLayout compositeDescLayout);
    };
}
