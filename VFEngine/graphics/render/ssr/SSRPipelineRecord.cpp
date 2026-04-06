#include "SSRPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include <glm/glm.hpp>

namespace render::ssr
{
    void SSRPipeline::execute(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        if (!initialized)
            return;

        updateParamsBuffer();

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            depthAspectMask);

        // Transition ssrRaw to ColorAttachmentOptimal for trace pass
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssrRawImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 1: Trace
        {
            auto colorAttach = core::colorClear(ssrRawImageView,
                vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));

            core::DynamicRenderingInfo info{};
            info.extent = traceExtent;
            info.colorAttachments = {colorAttach};

            core::beginDynamicRendering(commandBuffer, info);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, tracePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                tracePipelineLayout, 0, traceSet0PerImage[imageIndex], nullptr);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                tracePipelineLayout, 1, traceSet1, nullptr);
            commandBuffer.draw(3, 1, 0, 0);
            core::endDynamicRendering(commandBuffer);
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssrRawImage, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 2: Temporal accumulation
        {
            uint32_t readIdx = currentHistoryIdx;
            uint32_t writeIdx = 1 - currentHistoryIdx;

            if (!historyValid)
            {
                for (uint32_t i = 0; i < 2; i++)
                {
                    core::ImageUtilities::transitionImageLayout(commandBuffer,
                        ssrHistoryImages[i],
                        vk::ImageLayout::eUndefined,
                        vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::ImageAspectFlagBits::eColor);
                }
            }

            core::ImageUtilities::transitionImageLayout(commandBuffer,
                ssrHistoryImages[writeIdx],
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);

            auto colorAttach = core::colorClear(ssrHistoryImageViews[writeIdx],
                vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));

            core::DynamicRenderingInfo info{};
            info.extent = traceExtent;
            info.colorAttachments = {colorAttach};

            core::beginDynamicRendering(commandBuffer, info);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, temporalPipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                temporalPipelineLayout, 0, temporalSet0, nullptr);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                temporalPipelineLayout, 1, temporalSet1PerHistory[readIdx], nullptr);
            commandBuffer.draw(3, 1, 0, 0);
            core::endDynamicRendering(commandBuffer);

            core::ImageUtilities::transitionImageLayout(commandBuffer,
                ssrHistoryImages[writeIdx],
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);

            currentHistoryIdx = writeIdx;
            historyValid = true;
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssrDenoiseHorizImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 3a: Denoise horizontal
        {
            auto colorAttach = core::colorClear(ssrDenoiseHorizImageView,
                vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));

            core::DynamicRenderingInfo info{};
            info.extent = traceExtent;
            info.colorAttachments = {colorAttach};

            DenoisePushConstants denoisePush{};
            denoisePush.texelSize = glm::vec2(1.0f / static_cast<float>(traceExtent.width),
                                               1.0f / static_cast<float>(traceExtent.height));
            denoisePush.nearPlane = cachedNear;
            denoisePush.farPlane = cachedFar;
            denoisePush.direction = glm::vec2(1.0f, 0.0f);

            core::beginDynamicRendering(commandBuffer, info);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, denoisePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                denoisePipelineLayout, 0, denoiseHorizSet0PerHistory[currentHistoryIdx], nullptr);
            commandBuffer.pushConstants(denoisePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(DenoisePushConstants), &denoisePush);
            commandBuffer.draw(3, 1, 0, 0);
            core::endDynamicRendering(commandBuffer);
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssrDenoiseHorizImage, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssrDenoisedImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 3b: Denoise vertical
        {
            auto colorAttach = core::colorClear(ssrDenoisedImageView,
                vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));

            core::DynamicRenderingInfo info{};
            info.extent = traceExtent;
            info.colorAttachments = {colorAttach};

            DenoisePushConstants denoisePush{};
            denoisePush.texelSize = glm::vec2(1.0f / static_cast<float>(traceExtent.width),
                                               1.0f / static_cast<float>(traceExtent.height));
            denoisePush.nearPlane = cachedNear;
            denoisePush.farPlane = cachedFar;
            denoisePush.direction = glm::vec2(0.0f, 1.0f);

            core::beginDynamicRendering(commandBuffer, info);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, denoisePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                denoisePipelineLayout, 0, denoiseSet0, nullptr);
            commandBuffer.pushConstants(denoisePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(DenoisePushConstants), &denoisePush);
            commandBuffer.draw(3, 1, 0, 0);
            core::endDynamicRendering(commandBuffer);
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssrDenoisedImage, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 4: Composite (additive blend onto scene color)
        {
            vk::Image sceneColor = offscreenResources.colorImages[imageIndex].colorImage;
            core::ImageUtilities::transitionImageLayout(commandBuffer, sceneColor,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);

            auto colorAttach = core::colorLoad(offscreenResources.colorImages[imageIndex].colorImageView);

            core::DynamicRenderingInfo info{};
            info.extent = currentExtent;
            info.colorAttachments = {colorAttach};

            CompositePushConstants compositePush{};
            compositePush.intensity = ssrIntensity;
            compositePush.halfResolution = ssrHalfResolution ? 1u : 0u;
            compositePush.nearPlane = cachedNear;
            compositePush.farPlane = cachedFar;
            compositePush.texelSize = glm::vec2(1.0f / static_cast<float>(currentExtent.width),
                                                 1.0f / static_cast<float>(currentExtent.height));

            core::beginDynamicRendering(commandBuffer, info);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, compositePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                compositePipelineLayout, 0, compositeSet0, nullptr);
            commandBuffer.pushConstants(compositePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(CompositePushConstants), &compositePush);
            commandBuffer.draw(3, 1, 0, 0);
            core::endDynamicRendering(commandBuffer);

            core::ImageUtilities::transitionImageLayout(commandBuffer, sceneColor,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            depthAspectMask);
    }

    void SSRPipeline::executeGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        if (!initialized)
            return;

        updateParamsBuffer();

        // Graph provides color in ColorAttachmentOptimal, depth in DepthReadOnly.
        vk::Image sceneColor = offscreenResources.colorImages[imageIndex].colorImage;
        core::ImageUtilities::transitionImageLayout(commandBuffer, sceneColor,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Transition ssrRaw to ColorAttachmentOptimal
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssrRawImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 1: Trace
        {
            auto colorAttach = core::colorClear(ssrRawImageView,
                vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));

            core::DynamicRenderingInfo info{};
            info.extent = traceExtent;
            info.colorAttachments = {colorAttach};

            core::beginDynamicRendering(commandBuffer, info);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, tracePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                tracePipelineLayout, 0, traceSet0PerImage[imageIndex], nullptr);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                tracePipelineLayout, 1, traceSet1, nullptr);
            commandBuffer.draw(3, 1, 0, 0);
            core::endDynamicRendering(commandBuffer);
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssrRawImage, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 2: Temporal accumulation
        {
            uint32_t readIdx = currentHistoryIdx;
            uint32_t writeIdx = 1 - currentHistoryIdx;

            if (!historyValid)
            {
                for (uint32_t i = 0; i < 2; i++)
                {
                    core::ImageUtilities::transitionImageLayout(commandBuffer,
                        ssrHistoryImages[i],
                        vk::ImageLayout::eUndefined,
                        vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::ImageAspectFlagBits::eColor);
                }
            }

            core::ImageUtilities::transitionImageLayout(commandBuffer,
                ssrHistoryImages[writeIdx],
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);

            auto colorAttach = core::colorClear(ssrHistoryImageViews[writeIdx],
                vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));

            core::DynamicRenderingInfo info{};
            info.extent = traceExtent;
            info.colorAttachments = {colorAttach};

            core::beginDynamicRendering(commandBuffer, info);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, temporalPipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                temporalPipelineLayout, 0, temporalSet0, nullptr);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                temporalPipelineLayout, 1, temporalSet1PerHistory[readIdx], nullptr);
            commandBuffer.draw(3, 1, 0, 0);
            core::endDynamicRendering(commandBuffer);

            core::ImageUtilities::transitionImageLayout(commandBuffer,
                ssrHistoryImages[writeIdx],
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);

            currentHistoryIdx = writeIdx;
            historyValid = true;
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssrDenoiseHorizImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 3a: Denoise horizontal
        {
            auto colorAttach = core::colorClear(ssrDenoiseHorizImageView,
                vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));

            core::DynamicRenderingInfo info{};
            info.extent = traceExtent;
            info.colorAttachments = {colorAttach};

            DenoisePushConstants denoisePush{};
            denoisePush.texelSize = glm::vec2(1.0f / static_cast<float>(traceExtent.width),
                                               1.0f / static_cast<float>(traceExtent.height));
            denoisePush.nearPlane = cachedNear;
            denoisePush.farPlane = cachedFar;
            denoisePush.direction = glm::vec2(1.0f, 0.0f);

            core::beginDynamicRendering(commandBuffer, info);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, denoisePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                denoisePipelineLayout, 0, denoiseHorizSet0PerHistory[currentHistoryIdx], nullptr);
            commandBuffer.pushConstants(denoisePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(DenoisePushConstants), &denoisePush);
            commandBuffer.draw(3, 1, 0, 0);
            core::endDynamicRendering(commandBuffer);
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssrDenoiseHorizImage, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssrDenoisedImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 3b: Denoise vertical
        {
            auto colorAttach = core::colorClear(ssrDenoisedImageView,
                vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}));

            core::DynamicRenderingInfo info{};
            info.extent = traceExtent;
            info.colorAttachments = {colorAttach};

            DenoisePushConstants denoisePush{};
            denoisePush.texelSize = glm::vec2(1.0f / static_cast<float>(traceExtent.width),
                                               1.0f / static_cast<float>(traceExtent.height));
            denoisePush.nearPlane = cachedNear;
            denoisePush.farPlane = cachedFar;
            denoisePush.direction = glm::vec2(0.0f, 1.0f);

            core::beginDynamicRendering(commandBuffer, info);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, denoisePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                denoisePipelineLayout, 0, denoiseSet0, nullptr);
            commandBuffer.pushConstants(denoisePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(DenoisePushConstants), &denoisePush);
            commandBuffer.draw(3, 1, 0, 0);
            core::endDynamicRendering(commandBuffer);
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssrDenoisedImage, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 4: Composite — restore scene color for rendering
        core::ImageUtilities::transitionImageLayout(commandBuffer, sceneColor,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageAspectFlagBits::eColor);

        {
            auto colorAttach = core::colorLoad(offscreenResources.colorImages[imageIndex].colorImageView);

            core::DynamicRenderingInfo info{};
            info.extent = currentExtent;
            info.colorAttachments = {colorAttach};

            CompositePushConstants compositePush{};
            compositePush.intensity = ssrIntensity;
            compositePush.halfResolution = ssrHalfResolution ? 1u : 0u;
            compositePush.nearPlane = cachedNear;
            compositePush.farPlane = cachedFar;
            compositePush.texelSize = glm::vec2(1.0f / static_cast<float>(currentExtent.width),
                                                 1.0f / static_cast<float>(currentExtent.height));

            core::beginDynamicRendering(commandBuffer, info);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, compositePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                compositePipelineLayout, 0, compositeSet0, nullptr);
            commandBuffer.pushConstants(compositePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(CompositePushConstants), &compositePush);
            commandBuffer.draw(3, 1, 0, 0);
            core::endDynamicRendering(commandBuffer);
        }

        // Depth and scene color final transitions handled by render graph
    }
}
