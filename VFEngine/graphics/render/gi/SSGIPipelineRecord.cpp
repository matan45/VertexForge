#include "SSGIPipeline.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/OffScreen.hpp"
#include "../../core/ImageUtilities.hpp"
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

        // Pass 1: Trace
        {
            vk::ClearValue clearValue{};
            clearValue.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = traceRenderPass;
            rpBegin.framebuffer = traceFramebuffer;
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = traceExtent;
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearValue;

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, tracePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                tracePipelineLayout, 0, traceSet0PerImage[imageIndex], nullptr);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                tracePipelineLayout, 1, traceSet1, nullptr);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }

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

            vk::ClearValue clearValue{};
            clearValue.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = temporalRenderPass;
            rpBegin.framebuffer = temporalFramebuffers[writeIdx];
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = traceExtent;
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearValue;

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, temporalPipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                temporalPipelineLayout, 0, temporalSet0, nullptr);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                temporalPipelineLayout, 1, temporalSet1PerHistory[readIdx], nullptr);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();

            currentHistoryIdx = writeIdx;
            historyValid = true;
        }

        // Pass 3a: Denoise horizontal
        {
            vk::ClearValue clearValue{};
            clearValue.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = denoiseRenderPass;
            rpBegin.framebuffer = denoiseHorizFramebuffer;
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = traceExtent;
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearValue;

            DenoisePushConstants denoisePush{};
            denoisePush.texelSize = glm::vec2(1.0f / static_cast<float>(traceExtent.width),
                                               1.0f / static_cast<float>(traceExtent.height));
            denoisePush.nearPlane = cachedNear;
            denoisePush.farPlane = cachedFar;
            denoisePush.direction = glm::vec2(1.0f, 0.0f);

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, denoisePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                denoisePipelineLayout, 0, denoiseHorizSet0PerHistory[currentHistoryIdx], nullptr);
            commandBuffer.pushConstants(denoisePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(DenoisePushConstants), &denoisePush);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }

        // Pass 3b: Denoise vertical
        {
            vk::ClearValue clearValue{};
            clearValue.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = denoiseRenderPass;
            rpBegin.framebuffer = denoiseFramebuffer;
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = traceExtent;
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearValue;

            DenoisePushConstants denoisePush{};
            denoisePush.texelSize = glm::vec2(1.0f / static_cast<float>(traceExtent.width),
                                               1.0f / static_cast<float>(traceExtent.height));
            denoisePush.nearPlane = cachedNear;
            denoisePush.farPlane = cachedFar;
            denoisePush.direction = glm::vec2(0.0f, 1.0f);

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, denoisePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                denoisePipelineLayout, 0, denoiseSet0, nullptr);
            commandBuffer.pushConstants(denoisePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(DenoisePushConstants), &denoisePush);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }

        // Pass 4: Composite
        {
            vk::Image sceneColor = offscreenResources.colorImages[imageIndex].colorImage;
            core::ImageUtilities::transitionImageLayout(commandBuffer, sceneColor,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);

            CompositePushConstants compositePush{};
            compositePush.intensity = ssgiIntensity;
            compositePush.halfResolution = ssgiHalfResolution ? 1u : 0u;
            compositePush.nearPlane = cachedNear;
            compositePush.farPlane = cachedFar;
            compositePush.texelSize = glm::vec2(1.0f / static_cast<float>(currentExtent.width),
                                                 1.0f / static_cast<float>(currentExtent.height));

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = compositeRenderPass;
            rpBegin.framebuffer = compositeFramebuffers[imageIndex];
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = currentExtent;

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, compositePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                compositePipelineLayout, 0, compositeSet0, nullptr);
            commandBuffer.pushConstants(compositePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(CompositePushConstants), &compositePush);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
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

        // Depth and scene color transitions are handled by the render graph.

        // Pass 1: Trace
        {
            vk::ClearValue clearValue{};
            clearValue.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = traceRenderPass;
            rpBegin.framebuffer = traceFramebuffer;
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = traceExtent;
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearValue;

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, tracePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                tracePipelineLayout, 0, traceSet0PerImage[imageIndex], nullptr);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                tracePipelineLayout, 1, traceSet1, nullptr);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }

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

            vk::ClearValue clearValue{};
            clearValue.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = temporalRenderPass;
            rpBegin.framebuffer = temporalFramebuffers[writeIdx];
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = traceExtent;
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearValue;

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, temporalPipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                temporalPipelineLayout, 0, temporalSet0, nullptr);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                temporalPipelineLayout, 1, temporalSet1PerHistory[readIdx], nullptr);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();

            currentHistoryIdx = writeIdx;
            historyValid = true;
        }

        // Pass 3a: Denoise horizontal
        {
            vk::ClearValue clearValue{};
            clearValue.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = denoiseRenderPass;
            rpBegin.framebuffer = denoiseHorizFramebuffer;
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = traceExtent;
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearValue;

            DenoisePushConstants denoisePush{};
            denoisePush.texelSize = glm::vec2(1.0f / static_cast<float>(traceExtent.width),
                                               1.0f / static_cast<float>(traceExtent.height));
            denoisePush.nearPlane = cachedNear;
            denoisePush.farPlane = cachedFar;
            denoisePush.direction = glm::vec2(1.0f, 0.0f);

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, denoisePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                denoisePipelineLayout, 0, denoiseHorizSet0PerHistory[currentHistoryIdx], nullptr);
            commandBuffer.pushConstants(denoisePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(DenoisePushConstants), &denoisePush);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }

        // Pass 3b: Denoise vertical
        {
            vk::ClearValue clearValue{};
            clearValue.color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 0.0f}};

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = denoiseRenderPass;
            rpBegin.framebuffer = denoiseFramebuffer;
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = traceExtent;
            rpBegin.clearValueCount = 1;
            rpBegin.pClearValues = &clearValue;

            DenoisePushConstants denoisePush{};
            denoisePush.texelSize = glm::vec2(1.0f / static_cast<float>(traceExtent.width),
                                               1.0f / static_cast<float>(traceExtent.height));
            denoisePush.nearPlane = cachedNear;
            denoisePush.farPlane = cachedFar;
            denoisePush.direction = glm::vec2(0.0f, 1.0f);

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, denoisePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                denoisePipelineLayout, 0, denoiseSet0, nullptr);
            commandBuffer.pushConstants(denoisePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(DenoisePushConstants), &denoisePush);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }

        // Pass 4: Composite — transition scene color for the composite render pass
        {
            vk::Image sceneColor = offscreenResources.colorImages[imageIndex].colorImage;
            core::ImageUtilities::transitionImageLayout(commandBuffer, sceneColor,
                vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);

            CompositePushConstants compositePush{};
            compositePush.intensity = ssgiIntensity;
            compositePush.halfResolution = ssgiHalfResolution ? 1u : 0u;
            compositePush.nearPlane = cachedNear;
            compositePush.farPlane = cachedFar;
            compositePush.texelSize = glm::vec2(1.0f / static_cast<float>(currentExtent.width),
                                                 1.0f / static_cast<float>(currentExtent.height));

            vk::RenderPassBeginInfo rpBegin{};
            rpBegin.renderPass = compositeRenderPass;
            rpBegin.framebuffer = compositeFramebuffers[imageIndex];
            rpBegin.renderArea.offset = vk::Offset2D{0, 0};
            rpBegin.renderArea.extent = currentExtent;

            commandBuffer.beginRenderPass(rpBegin, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, compositePipeline);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                compositePipelineLayout, 0, compositeSet0, nullptr);
            commandBuffer.pushConstants(compositePipelineLayout,
                vk::ShaderStageFlagBits::eFragment, 0,
                sizeof(CompositePushConstants), &compositePush);
            commandBuffer.draw(3, 1, 0, 0);
            commandBuffer.endRenderPass();
        }
    }
}
