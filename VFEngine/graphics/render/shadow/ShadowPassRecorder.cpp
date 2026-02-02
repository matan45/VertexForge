#include "ShadowPassRecorder.hpp"
#include "ShadowAtlasManager.hpp"
#include "ShadowResourcePool.hpp"
#include "ShadowPassPipeline.hpp"
#include "TerrainShadowPipeline.hpp"
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
        const TerrainShadowPassParams* terrainParams,
        ShadowAtlasManager* atlasManager,
        ShadowResourcePool* resourcePool,
        ShadowPassPipeline* shadowPassPipeline,
        TerrainShadowPipeline* terrainShadowPipeline,
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

        // Check if terrain shadow rendering is available
        bool hasTerrainShadows = terrainParams != nullptr &&
                                  terrainParams->tileCount > 0 &&
                                  terrainShadowPipeline != nullptr &&
                                  terrainShadowPipeline->isInitialized();

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

        // Check if we have mesh batches to render
        bool hasMeshBatches = params.batchCount > 0 &&
                              params.commandsPerSection > 0 &&
                              params.drawCommandBuffer &&
                              params.drawCountBuffer;

        // If no mesh batches AND no terrain shadows, nothing to render
        if (!hasMeshBatches && !hasTerrainShadows)
            return;

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

            // Only bind mesh shadow pipeline and descriptors if we have mesh batches
            if (hasMeshBatches)
            {
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
            }

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

                // Render mesh shadows for this view
                if (hasMeshBatches)
                {
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

                // Render terrain shadows for this view
                if (hasTerrainShadows)
                {
                    terrainShadowPipeline->dispatch(
                        cmd,
                        terrainParams->terrainDataDescSet,
                        terrainParams->terrainMeshletDescSet,
                        terrainParams->terrainVertexDescSet,
                        view->viewProjectionMatrix,
                        terrainParams->tileCount,
                        terrainParams->shadowLOD,
                        view->depthBias,
                        view->slopeBias
                    );
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
        renderPointLightCubeShadows(cmd, params, terrainParams, resourcePool, shadowPassPipeline,
                                     terrainShadowPipeline, lightShadowData);
    }

    void ShadowPassRecorder::renderPointLightCubeShadows(
        vk::CommandBuffer cmd,
        const ShadowPassParams& params,
        const TerrainShadowPassParams* terrainParams,
        ShadowResourcePool* resourcePool,
        ShadowPassPipeline* shadowPassPipeline,
        TerrainShadowPipeline* terrainShadowPipeline,
        std::unordered_map<uint32_t, LightShadowData>& lightShadowData)
    {
        if (!resourcePool || !shadowPassPipeline)
            return;

        // Check if terrain shadow rendering is available
        bool hasTerrainShadows = terrainParams != nullptr &&
                                  terrainParams->tileCount > 0 &&
                                  terrainShadowPipeline != nullptr &&
                                  terrainShadowPipeline->isInitialized();

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

        // Check if we have mesh batches to render
        bool hasMeshBatches = params.batchCount > 0 &&
                              params.commandsPerSection > 0 &&
                              params.drawCommandBuffer &&
                              params.drawCountBuffer;

        // If no mesh batches AND no terrain shadows, nothing to render for point lights
        if (!hasMeshBatches && !hasTerrainShadows)
            return;

        std::array<vk::DescriptorSet, 4> meshDescriptorSets = {
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

            for (uint32_t face = 0; face < ShadowConstants::CUBE_FACE_COUNT; ++face)
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

                cmd.setViewport(0, 1, &viewport);
                cmd.setScissor(0, 1, &scissor);
                cmd.setDepthBias(view.depthBias, 0.0f, view.slopeBias);

                // Render mesh shadows for this cube face
                if (hasMeshBatches)
                {
                    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, shadowPassPipeline->getPipeline());
                    cmd.bindDescriptorSets(
                        vk::PipelineBindPoint::eGraphics,
                        shadowPassPipeline->getPipelineLayout(),
                        0,
                        static_cast<uint32_t>(meshDescriptorSets.size()),
                        meshDescriptorSets.data(),
                        0, nullptr
                    );

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
                }

                // Render terrain shadows for this cube face
                if (hasTerrainShadows)
                {
                    terrainShadowPipeline->dispatch(
                        cmd,
                        terrainParams->terrainDataDescSet,
                        terrainParams->terrainMeshletDescSet,
                        terrainParams->terrainVertexDescSet,
                        view.viewProjectionMatrix,
                        terrainParams->tileCount,
                        terrainParams->shadowLOD,
                        view.depthBias,
                        view.slopeBias
                    );
                }

                cmd.endRenderPass();
            }

            cube->transitionToShaderRead(cmd);
        }
    }
}
