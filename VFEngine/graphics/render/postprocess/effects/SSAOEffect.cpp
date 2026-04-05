#include "SSAOEffect.hpp"
#include "../PostProcessPipeline.hpp"
#include "../../../core/Device.hpp"
#include "../../../core/SwapChain.hpp"
#include "../../../core/Shader.hpp"
#include "../../../core/OffScreen.hpp"
#include "../../../core/ImageUtilities.hpp"
#include "../../../core/BufferUtilities.hpp"
#include "../../../core/DynamicRenderingHelpers.hpp"

namespace render::postprocess
{
    SSAOEffect::SSAOEffect(core::Device& device, core::SwapChain& swapChain,
                           core::OffscreenResources& offscreenResources,
                           PostProcessPipeline& pipeline)
        : device{device}, swapChain{swapChain},
          offscreenResources{offscreenResources}, pipeline{pipeline}
    {
        enabled = false;

        vk::Format depthFormat = swapChain.getSwapchainDepthStencilFormat();
        depthAspectMask = vk::ImageAspectFlagBits::eDepth;
        if (depthFormat == vk::Format::eD16UnormS8Uint ||
            depthFormat == vk::Format::eD24UnormS8Uint ||
            depthFormat == vk::Format::eD32SfloatS8Uint)
        {
            depthAspectMask |= vk::ImageAspectFlagBits::eStencil;
        }
    }

    void SSAOEffect::init(vk::Format colorFormat, vk::Extent2D extent)
    {
        currentExtent = extent;

        createSampler();
        createImages();
        createDepthImageView();
        createParamsBuffer();
        createDescriptorSetLayouts();
        createDescriptorPool();
        createDescriptorSets();
        loadShaders();
        createSSAOPipeline();
        createBlurPipeline();
        createCompositePipeline(colorFormat);

        initialized = true;
    }

    void SSAOEffect::cleanup()
    {
        auto& dev = device.getLogicalDevice();

        cleanupPipelines();
        cleanupImages();

        if (depthOnlyImageView)
        {
            dev.destroyImageView(depthOnlyImageView);
            depthOnlyImageView = nullptr;
        }

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        if (ssaoDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(ssaoDescriptorSetLayout);
            ssaoDescriptorSetLayout = nullptr;
        }

        if (blurDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(blurDescriptorSetLayout);
            blurDescriptorSetLayout = nullptr;
        }

        if (compositeDescriptorSetLayout)
        {
            dev.destroyDescriptorSetLayout(compositeDescriptorSetLayout);
            compositeDescriptorSetLayout = nullptr;
        }

        if (paramsBuffer)
        {
            paramsBufferMapped = nullptr;
            core::BufferUtilities::destroyBuffer(dev, paramsBuffer, paramsBufferAllocation, device.getMemoryManager());
        }

        if (sampler)
        {
            dev.destroySampler(sampler);
            sampler = nullptr;
        }

        if (ssaoShader) { ssaoShader->cleanUp(); ssaoShader.reset(); }
        if (blurShader) { blurShader->cleanUp(); blurShader.reset(); }
        if (compositeShader) { compositeShader->cleanUp(); compositeShader.reset(); }

        initialized = false;
    }

    void SSAOEffect::recreate(vk::Format colorFormat, vk::Extent2D extent)
    {
        currentExtent = extent;
        auto& dev = device.getLogicalDevice();

        cleanupPipelines();
        cleanupImages();

        if (depthOnlyImageView)
        {
            dev.destroyImageView(depthOnlyImageView);
            depthOnlyImageView = nullptr;
        }

        if (descriptorPool)
        {
            dev.destroyDescriptorPool(descriptorPool);
            descriptorPool = nullptr;
        }

        createImages();
        createDepthImageView();
        createDescriptorPool();
        createDescriptorSets();
        createSSAOPipeline();
        createBlurPipeline();
        createCompositePipeline(colorFormat);
    }

    void SSAOEffect::preRecord(const vk::CommandBuffer& commandBuffer,
                                vk::DescriptorSet inputDescriptorSet)
    {
        updateParamsBuffer();

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            depthAspectMask);

        // Pass 1: SSAO calculation
        {
            auto colorAttach = core::colorDontCare(ssaoRawImageView);

            core::DynamicRenderingInfo dynInfo{};
            dynInfo.extent = currentExtent;
            dynInfo.colorAttachments = {colorAttach};

            core::beginDynamicRendering(commandBuffer, dynInfo);

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, ssaoPipeline);

            std::array<vk::DescriptorSet, 2> sets = {inputDescriptorSet, ssaoDescriptorSet};
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                              ssaoPipelineLayout, 0,
                                              static_cast<uint32_t>(sets.size()),
                                              sets.data(), 0, nullptr);

            commandBuffer.draw(3, 1, 0, 0);
            core::endDynamicRendering(commandBuffer);

            // Transition SSAO raw to shader read for blur pass
            core::ImageUtilities::transitionImageLayout(commandBuffer, ssaoRawImage,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
        }

        // Pass 2: Bilateral blur
        {
            auto colorAttach = core::colorDontCare(ssaoBlurredImageView);

            core::DynamicRenderingInfo dynInfo{};
            dynInfo.extent = currentExtent;
            dynInfo.colorAttachments = {colorAttach};

            core::beginDynamicRendering(commandBuffer, dynInfo);

            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, blurPipeline);

            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                              blurPipelineLayout, 0,
                                              1, &blurDescriptorSet, 0, nullptr);

            const auto& camInfo = pipeline.getCameraData();
            struct BlurPushConstants { float nearPlane; float farPlane; } blurPC{};
            blurPC.nearPlane = camInfo.nearPlane;
            blurPC.farPlane = camInfo.farPlane;
            commandBuffer.pushConstants(blurPipelineLayout,
                                         vk::ShaderStageFlagBits::eFragment,
                                         0, sizeof(BlurPushConstants), &blurPC);

            commandBuffer.draw(3, 1, 0, 0);
            core::endDynamicRendering(commandBuffer);

            // Transition blurred SSAO to shader read for composite pass
            core::ImageUtilities::transitionImageLayout(commandBuffer, ssaoBlurredImage,
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            depthAspectMask);
    }

    void SSAOEffect::record(const vk::CommandBuffer& commandBuffer,
                              vk::DescriptorSet inputDescriptorSet)
    {
        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, compositePipeline);

        std::array<vk::DescriptorSet, 2> sets = {inputDescriptorSet, compositeDescriptorSet};
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                          compositePipelineLayout, 0,
                                          static_cast<uint32_t>(sets.size()),
                                          sets.data(), 0, nullptr);

        struct CompositePushConstants { float intensity; } compositePC{};
        compositePC.intensity = currentIntensity;
        commandBuffer.pushConstants(compositePipelineLayout,
                                     vk::ShaderStageFlagBits::eFragment,
                                     0, sizeof(CompositePushConstants), &compositePC);

        commandBuffer.draw(3, 1, 0, 0);
    }

    void SSAOEffect::updateParameters(const ::postprocess::PostProcessSettings& settings)
    {
        const auto& s = settings.ssao;
        enabled = s.enabled;
        currentRadius = s.radius;
        currentBias = s.bias;
        currentIntensity = s.intensity;
        currentKernelSize = s.kernelSize;
        currentPower = s.power;
    }
}
