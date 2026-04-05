#include "SSGIPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
#include "../../core/DynamicRenderingHelpers.hpp"
#include <glm/glm.hpp>

namespace render::gi
{
    void SSGIPipeline::execute(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        if (!initialized)
            return;

        updateParamsBuffer();

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            offscreenResources.depthImage.depthImage,
            vk::ImageLayout::eDepthStencilAttachmentOptimal,
            vk::ImageLayout::eDepthStencilReadOnlyOptimal,
            depthAspectMask);

        // Transition ssgiRaw to ColorAttachmentOptimal for trace pass
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssgiRawImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 1: Trace
        {
            auto colorAttach = core::colorClear(ssgiRawImageView,
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

        // Transition ssgiRaw to ShaderReadOnlyOptimal for temporal pass input
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssgiRawImage, vk::ImageLayout::eColorAttachmentOptimal,
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
                        ssgiHistoryImages[i],
                        vk::ImageLayout::eUndefined,
                        vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::ImageAspectFlagBits::eColor);
                }
            }

            // Transition write history to ColorAttachmentOptimal
            core::ImageUtilities::transitionImageLayout(commandBuffer,
                ssgiHistoryImages[writeIdx],
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);

            auto colorAttach = core::colorClear(ssgiHistoryImageViews[writeIdx],
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

            // Transition write history to ShaderReadOnlyOptimal
            core::ImageUtilities::transitionImageLayout(commandBuffer,
                ssgiHistoryImages[writeIdx],
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);

            currentHistoryIdx = writeIdx;
            historyValid = true;
        }

        // Transition denoiseHoriz to ColorAttachmentOptimal
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssgiDenoiseHorizImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 3a: Denoise horizontal
        {
            auto colorAttach = core::colorClear(ssgiDenoiseHorizImageView,
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

        // Transition denoiseHoriz to ShaderReadOnlyOptimal, denoised to ColorAttachmentOptimal
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssgiDenoiseHorizImage, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssgiDenoisedImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 3b: Denoise vertical
        {
            auto colorAttach = core::colorClear(ssgiDenoisedImageView,
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

        // Transition denoised to ShaderReadOnlyOptimal for composite read
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssgiDenoisedImage, vk::ImageLayout::eColorAttachmentOptimal,
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
            compositePush.intensity = ssgiIntensity;
            compositePush.halfResolution = ssgiHalfResolution ? 1u : 0u;
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

            // Transition scene color back to ShaderReadOnlyOptimal
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

    void SSGIPipeline::executeGraphManaged(const vk::CommandBuffer& commandBuffer, uint32_t imageIndex)
    {
        if (!initialized)
            return;

        updateParamsBuffer();

        // Graph provides color in ColorAttachmentOptimal, depth in DepthReadOnly.
        // Transition color to ShaderReadOnly for trace sampling, restore before composite.
        // Depth is already at ReadOnly from graph — no transition needed.
        vk::Image sceneColor = offscreenResources.colorImages[imageIndex].colorImage;
        core::ImageUtilities::transitionImageLayout(commandBuffer, sceneColor,
            vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eColor);

        // Transition ssgiRaw to ColorAttachmentOptimal
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssgiRawImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 1: Trace
        {
            auto colorAttach = core::colorClear(ssgiRawImageView,
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

        // Transition ssgiRaw to ShaderReadOnly
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssgiRawImage, vk::ImageLayout::eColorAttachmentOptimal,
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
                        ssgiHistoryImages[i],
                        vk::ImageLayout::eUndefined,
                        vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::ImageAspectFlagBits::eColor);
                }
            }

            core::ImageUtilities::transitionImageLayout(commandBuffer,
                ssgiHistoryImages[writeIdx],
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);

            auto colorAttach = core::colorClear(ssgiHistoryImageViews[writeIdx],
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
                ssgiHistoryImages[writeIdx],
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);

            currentHistoryIdx = writeIdx;
            historyValid = true;
        }

        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssgiDenoiseHorizImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 3a: Denoise horizontal
        {
            auto colorAttach = core::colorClear(ssgiDenoiseHorizImageView,
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
            ssgiDenoiseHorizImage, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);
        core::ImageUtilities::transitionImageLayout(commandBuffer,
            ssgiDenoisedImage, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 3b: Denoise vertical
        {
            auto colorAttach = core::colorClear(ssgiDenoisedImageView,
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
            ssgiDenoisedImage, vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageAspectFlagBits::eColor);

        // Pass 4: Composite — restore scene color and depth for rendering
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
            compositePush.intensity = ssgiIntensity;
            compositePush.halfResolution = ssgiHalfResolution ? 1u : 0u;
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
