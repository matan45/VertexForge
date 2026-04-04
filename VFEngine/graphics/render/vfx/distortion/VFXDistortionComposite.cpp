#include "VFXDistortionComposite.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/PipelineUtilities.hpp"
#include "../../../core/DynamicRenderingHelpers.hpp"
#include "print/Log.hpp"

namespace render::vfx
{
    VFXDistortionComposite::VFXDistortionComposite(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain)
    {
    }

    VFXDistortionComposite::~VFXDistortionComposite()
    {
        cleanup();
    }

    void VFXDistortionComposite::init(vk::Format colorFormat, vk::DescriptorSetLayout compositeDescLayout)
    {
        loadShader();
        createPipeline(colorFormat, compositeDescLayout);
        initialized = true;
    }

    void VFXDistortionComposite::recreate(vk::Format colorFormat, vk::DescriptorSetLayout compositeDescLayout)
    {
        cleanup();
        init(colorFormat, compositeDescLayout);
    }

    void VFXDistortionComposite::cleanup()
    {
        if (!initialized) return;

        auto vkDevice = device.getLogicalDevice();
        if (pipeline) { vkDevice.destroyPipeline(pipeline); pipeline = nullptr; }
        if (pipelineLayout) { vkDevice.destroyPipelineLayout(pipelineLayout); pipelineLayout = nullptr; }
        compositeShader.reset();
        initialized = false;
    }

    void VFXDistortionComposite::loadShader()
    {
        compositeShader = std::make_shared<core::Shader>(device);
        compositeShader->readShader("../../resources/shaders/vfx/vfx_distortion_composite.glsl");

        if (compositeShader->getShaderStages().empty())
        {
            vfLogError("VFXDistortionComposite: Failed to load shader: {}",
                        compositeShader->getLastCompilationError());
        }
    }

    void VFXDistortionComposite::createPipeline(vk::Format colorFormat, vk::DescriptorSetLayout compositeDescLayout)
    {
        // Fullscreen triangle: no vertex inputs
        core::GraphicsPipelineConfig config{
            .device = device.getLogicalDevice(),
            .renderPass = nullptr,
            .extent = swapChain.getSwapchainExtent(),
            .colorAttachmentFormats = {colorFormat},
            .shaderStages = compositeShader->getShaderStages(),
            .topology = vk::PrimitiveTopology::eTriangleList,
            .descriptorSetLayouts = {compositeDescLayout},
            .cullMode = vk::CullModeFlagBits::eNone,
            .depthTestEnable = false,
            .depthWriteEnable = false,
            .blendEnable = false,
            .dynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor}
        };

        auto result = core::PipelineUtilities::createGraphicsPipeline(config);
        pipeline = result.pipeline;
        pipelineLayout = result.pipelineLayout;
    }

    void VFXDistortionComposite::record(
        vk::CommandBuffer cmd,
        vk::ImageView colorImageView,
        vk::Extent2D extent,
        vk::DescriptorSet compositeDescSet) const
    {
        if (!initialized) return;

        auto colorAttach = core::colorLoad(colorImageView);

        core::DynamicRenderingInfo dynInfo{};
        dynInfo.extent = extent;
        dynInfo.colorAttachments = {colorAttach};

        core::beginDynamicRendering(cmd, dynInfo);

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline);

        vk::Viewport viewport{0.0f, 0.0f,
            static_cast<float>(extent.width), static_cast<float>(extent.height),
            0.0f, 1.0f};
        cmd.setViewport(0, viewport);
        vk::Rect2D scissor{{0, 0}, extent};
        cmd.setScissor(0, scissor);

        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipelineLayout,
                               0, compositeDescSet, {});

        // Fullscreen triangle: 3 vertices, no vertex buffer
        cmd.draw(3, 1, 0, 0);

        core::endDynamicRendering(cmd);
    }
}
