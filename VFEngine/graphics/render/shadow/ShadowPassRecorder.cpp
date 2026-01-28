#include "ShadowPassRecorder.hpp"
#include "ShadowAtlasManager.hpp"
#include "ShadowResourcePool.hpp"
#include "ShadowPassPipeline.hpp"
#include "../../core/Device.hpp"
#include "print/Logger.hpp"

namespace render::shadow
{
    ShadowPassRecorder::ShadowPassRecorder(core::Device& device)
        : device(device)
    {
    }

    void ShadowPassRecorder::recordShadowPass(
        vk::CommandBuffer cmd,
        const ShadowPassParams& params,
        ShadowAtlasManager* atlasManager,
        ShadowResourcePool* resourcePool,
        ShadowPassPipeline* shadowPassPipeline,
        const std::vector<ShadowView>& directionalShadowViews,
        const std::vector<ShadowView>& spotShadowViews,
        std::unordered_map<uint32_t, LightShadowData>& lightShadowData,
        bool shadowsEnabled)
    {
        if (!shadowsEnabled || !shadowPassPipeline || !shadowPassPipeline->isInitialized())
        {
            loggerWarning("ShadowPassRecorder::recordShadowPass: Not ready (enabled={}, pipeline={}, init={})",
                         shadowsEnabled, shadowPassPipeline != nullptr,
                         shadowPassPipeline ? shadowPassPipeline->isInitialized() : false);
            return;
        }

        std::vector<const ShadowView*> allViews;
        for (const auto& view : spotShadowViews)
            allViews.push_back(&view);
        for (const auto& view : directionalShadowViews)
            allViews.push_back(&view);

        bool hasAtlasViews = !allViews.empty();
        bool hasPointShadows = false;
        for (const auto& [entityId, data] : lightShadowData)
        {
            if (data.type == ShadowMapType::PointCube &&
                data.settings.enabled && data.settings.castShadows &&
                data.resourceHandle.isValid())
            {
                hasPointShadows = true;
                break;
            }
        }

        if (!hasAtlasViews && !hasPointShadows)
            return;

        if (params.batchCount == 0 || params.commandsPerSection == 0)
            return;

        if (!params.drawCommandBuffer || !params.drawCountBuffer)
        {
            loggerWarning("ShadowPassRecorder::recordShadowPass: Invalid draw buffers");
            return;
        }

#ifndef NDEBUG
        if (!params.perDrawDataDescSet || !params.meshletDataDescSet ||
            !params.vertexDataDescSet || !params.boneMatrixDescSet)
        {
            loggerError("ShadowPassRecorder::recordShadowPass: Invalid descriptor set(s) - "
                          "perDraw={}, meshlet={}, vertex={}, bone={}",
                          static_cast<bool>(params.perDrawDataDescSet),
                          static_cast<bool>(params.meshletDataDescSet),
                          static_cast<bool>(params.vertexDataDescSet),
                          static_cast<bool>(params.boneMatrixDescSet));
            return;
        }
#endif

        // Atlas rendering (spot and directional lights)
        if (hasAtlasViews)
        {
            // Transition atlas to depth attachment
            {
                vk::ImageMemoryBarrier barrier{};
                barrier.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
                barrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = atlasManager->getAtlasImage();
                barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;

                vk::PipelineStageFlags srcStage;
                if (atlasFirstUse)
                {
                    barrier.srcAccessMask = {};
                    barrier.oldLayout = vk::ImageLayout::eUndefined;
                    srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
                }
                else
                {
                    barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
                    barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                    srcStage = vk::PipelineStageFlagBits::eFragmentShader;
                }

                cmd.pipelineBarrier(
                    srcStage,
                    vk::PipelineStageFlagBits::eEarlyFragmentTests,
                    {},
                    0, nullptr,
                    0, nullptr,
                    1, &barrier
                );
            }

            // Begin render pass
            vk::ClearValue clearValue{};
            clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

            vk::RenderPassBeginInfo renderPassInfo{};
            renderPassInfo.renderPass = shadowPassPipeline->getRenderPass();
            renderPassInfo.framebuffer = shadowPassPipeline->getFramebuffer();
            renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
            renderPassInfo.renderArea.extent = vk::Extent2D{
                atlasManager->getAtlasWidth(), atlasManager->getAtlasHeight()
            };
            renderPassInfo.clearValueCount = 1;
            renderPassInfo.pClearValues = &clearValue;

            cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

            cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, shadowPassPipeline->getPipeline());

            std::array<vk::DescriptorSet, 4> descriptorSets = {
                params.perDrawDataDescSet,
                params.meshletDataDescSet,
                params.vertexDataDescSet,
                params.boneMatrixDescSet
            };
            cmd.bindDescriptorSets(
                vk::PipelineBindPoint::eGraphics,
                shadowPassPipeline->getPipelineLayout(),
                0,
                static_cast<uint32_t>(descriptorSets.size()),
                descriptorSets.data(),
                0, nullptr
            );

            // Render each shadow view
            for (const auto* view : allViews)
            {
                if (!view->handle.isValid())
                {
                    loggerWarning("ShadowPassRecorder: Skipping view with invalid handle");
                    continue;
                }

                vk::Viewport viewport = atlasManager->getPixelViewport(view->handle);
                cmd.setViewport(0, 1, &viewport);

                vk::Rect2D scissor = atlasManager->getScissorRect(view->handle);
                cmd.setScissor(0, 1, &scissor);

                cmd.setDepthBias(view->depthBias, 0.0f, view->slopeBias);

                ShadowPushConstants pc{};
                pc.lightViewProjection = view->viewProjectionMatrix;
                pc.baseDrawIndex = 0;
                pc.depthBias = view->depthBias;
                pc.slopeBias = view->slopeBias;
                pc.normalBias = view->normalBias;

                for (uint32_t shaderGroup = 0; shaderGroup < params.shaderGroupCount; ++shaderGroup)
                {
                    for (uint32_t batch = 0; batch < params.batchCount; ++batch)
                    {
                        uint32_t sectionIndex = batch * params.shaderGroupCount + shaderGroup;
                        pc.baseDrawIndex = sectionIndex * params.commandsPerSection;

                        cmd.pushConstants(
                            shadowPassPipeline->getPipelineLayout(),
                            vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
                            0,
                            sizeof(ShadowPushConstants),
                            &pc
                        );

                        vk::DeviceSize commandOffset = sectionIndex * params.commandsPerSection * sizeof(
                            vk::DrawMeshTasksIndirectCommandEXT);
                        vk::DeviceSize countOffset = sectionIndex * params.drawCountStructSize;

                        cmd.drawMeshTasksIndirectCountEXT(
                            params.drawCommandBuffer,
                            commandOffset,
                            params.drawCountBuffer,
                            countOffset,
                            params.commandsPerSection,
                            sizeof(vk::DrawMeshTasksIndirectCommandEXT)
                        );
                    }
                }
            }

            cmd.endRenderPass();

            // Transition atlas back to shader read
            {
                vk::ImageMemoryBarrier barrier{};
                barrier.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
                barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
                barrier.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
                barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.image = atlasManager->getAtlasImage();
                barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;

                cmd.pipelineBarrier(
                    vk::PipelineStageFlagBits::eLateFragmentTests,
                    vk::PipelineStageFlagBits::eFragmentShader,
                    {},
                    0, nullptr,
                    0, nullptr,
                    1, &barrier
                );
            }

            atlasFirstUse = false;
        }

        // Point light cube shadow rendering
        renderPointLightCubeShadows(cmd, params, resourcePool, shadowPassPipeline, lightShadowData);
    }

    void ShadowPassRecorder::renderPointLightCubeShadows(
        vk::CommandBuffer cmd,
        const ShadowPassParams& params,
        ShadowResourcePool* resourcePool,
        ShadowPassPipeline* shadowPassPipeline,
        std::unordered_map<uint32_t, LightShadowData>& lightShadowData)
    {
        if (!resourcePool || !shadowPassPipeline)
            return;

        const auto& logicalDevice = device.getLogicalDevice();

        std::vector<std::pair<uint32_t, LightShadowData*>> pointLightsToRender;

        for (auto& [entityId, data] : lightShadowData)
        {
            if (data.type != ShadowMapType::PointCube ||
                !data.settings.enabled || !data.settings.castShadows ||
                !data.resourceHandle.isValid())
                continue;

            ShadowCubeMap* cube = resourcePool->getCube(data.resourceHandle);
            if (!cube || !cube->isInitialized())
                continue;

            pointLightsToRender.emplace_back(entityId, &data);
        }

        if (pointLightsToRender.empty())
            return;

        std::array<vk::DescriptorSet, 4> descriptorSets = {
            params.perDrawDataDescSet,
            params.meshletDataDescSet,
            params.vertexDataDescSet,
            params.boneMatrixDescSet
        };

        for (auto& [entityId, data] : pointLightsToRender)
        {
            ShadowCubeMap* cube = resourcePool->getCube(data->resourceHandle);
            uint32_t cubeSize = cube->getSize();

            cube->transitionToDepthAttachment(cmd);

            vk::Viewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(cubeSize);
            viewport.height = static_cast<float>(cubeSize);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            vk::Rect2D scissor{};
            scissor.offset = vk::Offset2D{0, 0};
            scissor.extent = vk::Extent2D{cubeSize, cubeSize};

            for (uint32_t face = 0; face < ShadowCubeMap::FACE_COUNT; ++face)
            {
                if (face >= data->views.size())
                    continue;

                const auto& view = data->views[face];

                vk::Framebuffer faceFramebuffer = cube->getOrCreateFramebuffer(
                    face, shadowPassPipeline->getRenderPass(), logicalDevice);

                vk::ClearValue clearValue{};
                clearValue.depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

                vk::RenderPassBeginInfo renderPassInfo{};
                renderPassInfo.renderPass = shadowPassPipeline->getRenderPass();
                renderPassInfo.framebuffer = faceFramebuffer;
                renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
                renderPassInfo.renderArea.extent = vk::Extent2D{cubeSize, cubeSize};
                renderPassInfo.clearValueCount = 1;
                renderPassInfo.pClearValues = &clearValue;

                cmd.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

                cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, shadowPassPipeline->getPipeline());
                cmd.bindDescriptorSets(
                    vk::PipelineBindPoint::eGraphics,
                    shadowPassPipeline->getPipelineLayout(),
                    0,
                    static_cast<uint32_t>(descriptorSets.size()),
                    descriptorSets.data(),
                    0, nullptr
                );

                cmd.setViewport(0, 1, &viewport);
                cmd.setScissor(0, 1, &scissor);

                cmd.setDepthBias(view.depthBias, 0.0f, view.slopeBias);

                ShadowPushConstants pc{};
                pc.lightViewProjection = view.viewProjectionMatrix;
                pc.depthBias = view.depthBias;
                pc.slopeBias = view.slopeBias;
                pc.normalBias = view.normalBias;

                for (uint32_t shaderGroup = 0; shaderGroup < params.shaderGroupCount; ++shaderGroup)
                {
                    for (uint32_t batch = 0; batch < params.batchCount; ++batch)
                    {
                        uint32_t sectionIndex = batch * params.shaderGroupCount + shaderGroup;
                        pc.baseDrawIndex = sectionIndex * params.commandsPerSection;

                        cmd.pushConstants(
                            shadowPassPipeline->getPipelineLayout(),
                            vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT,
                            0,
                            sizeof(ShadowPushConstants),
                            &pc
                        );

                        vk::DeviceSize commandOffset = sectionIndex * params.commandsPerSection * sizeof(
                            vk::DrawMeshTasksIndirectCommandEXT);
                        vk::DeviceSize countOffset = sectionIndex * params.drawCountStructSize;

                        cmd.drawMeshTasksIndirectCountEXT(
                            params.drawCommandBuffer,
                            commandOffset,
                            params.drawCountBuffer,
                            countOffset,
                            params.commandsPerSection,
                            sizeof(vk::DrawMeshTasksIndirectCommandEXT)
                        );
                    }
                }

                cmd.endRenderPass();
            }

            cube->transitionToShaderRead(cmd);
        }
    }
}
