#include "GPUDrivenRenderer.hpp"
#include "../../core/SwapChain.hpp"
#include "../gi/GIDebugRenderer.hpp"
#include "../gi/RadianceCascadeManager.hpp"
#include "../gi/ProbeStorageBuffer.hpp"
#include <array>
#include <vector>

namespace render::gpudriven
{
    static std::vector<vk::DescriptorSet> buildDescriptorSets(
        vk::DescriptorSet iblSet, MeshShaderPipeline& pipeline,
        BindlessTextureManager& bindless, BoneMatrixManager& bones)
    {
        std::vector<vk::DescriptorSet> sets = {
            iblSet,
            pipeline.getPerDrawDataDescriptorSet(),
            bindless.getDescriptorSet(),
            pipeline.getMeshletDataDescriptorSet(),
            pipeline.getVertexDataDescriptorSet(),
            bones.getDescriptorSet(),
            pipeline.getLightDataDescriptorSet(),
            pipeline.getClusterGridDescriptorSet(),
            pipeline.getCullingOutputDescriptorSet(),
            pipeline.getShadowDataDescriptorSet(),
            pipeline.getShadowTextureDescriptorSet()
        };
        if (pipeline.getGIProbeDataDescriptorSet())
        {
            sets.push_back(pipeline.getGIProbeDataDescriptorSet());
        }
        return sets;
    }

    static void bindCausticDescriptorSet(vk::CommandBuffer cmd, vk::PipelineLayout layout,
                                          MeshShaderPipeline& pipeline)
    {
        vk::DescriptorSet causticSet = pipeline.getCausticDescriptorSet();
        if (causticSet)
        {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 12, 1, &causticSet, 0, nullptr);
        }
    }

    // Binds the shared RT shadow mask set at set 13 (directional/spot/point in bindings 0/1/2).
    // The set is ringed per swapchain image (VK-1398): sync + bind the slot for imageIndex.
    static void bindRTShadowMaskDescriptorSet(vk::CommandBuffer cmd, vk::PipelineLayout layout,
                                               MeshShaderPipeline& pipeline, uint32_t imageIndex)
    {
        if (!pipeline.hasRTMask())
            return;

        pipeline.ensureRTMaskSlot(imageIndex);
        vk::DescriptorSet rtMaskSet = pipeline.getRTMaskDescriptorSet(imageIndex);
        if (rtMaskSet)
        {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout, 13, 1, &rtMaskSet, 0, nullptr);
        }
    }

    static void bindWorldMaskDescriptorSet(vk::CommandBuffer cmd, vk::PipelineLayout layout,
                                            MeshShaderPipeline& pipeline)
    {
        if (!pipeline.hasWorldMaskLayout())
            return;

        vk::DescriptorSet worldMaskSet = pipeline.getWorldMaskDescriptorSet();
        if (worldMaskSet)
        {
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, layout,
                                   pipeline.getWorldMaskSetIndex(), 1, &worldMaskSet, 0, nullptr);
        }
    }

    // Spot (VK-1175) and point (VK-1176) RT masks now ride in the shared set 13 alongside the
    // directional mask, so they no longer need their own bind at sets 15/16 (see
    // bindRTShadowMaskDescriptorSet).

    void GPUDrivenRenderer::renderDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                       uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!initialized || !enabled || stats.totalObjects == 0 || !meshShaderPipeline)
        {
            return;
        }

        uint32_t batchCount = batchManager->getBatchCount();
        uint32_t commandsPerSection = batchManager->getCommandsPerSection();

        vk::Pipeline activePipeline = meshShaderPipeline->getPipeline();
        vk::PipelineLayout layout = meshShaderPipeline->getPipelineLayout();

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, activePipeline);

        auto descriptorSets = buildDescriptorSets(
            iblDescriptorSet, *meshShaderPipeline, *bindlessTextures, *boneMatrixManager);

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            layout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        bindCausticDescriptorSet(cmd, layout, *meshShaderPipeline);
        bindRTShadowMaskDescriptorSet(cmd, layout, *meshShaderPipeline, currentImageIndex);
        bindWorldMaskDescriptorSet(cmd, layout, *meshShaderPipeline);

        float dispatchWidth, dispatchHeight;
        if (screenWidth > 0 && screenHeight > 0)
        {
            dispatchWidth = static_cast<float>(screenWidth);
            dispatchHeight = static_cast<float>(screenHeight);
        }
        else
        {
            auto extent = swapChain.getSwapchainExtent();
            dispatchWidth = static_cast<float>(extent.width);
            dispatchHeight = static_cast<float>(extent.height);
        }

        for (uint32_t shaderGroup = 0; shaderGroup <= 2; ++shaderGroup)
        {
            for (uint32_t batch = 0; batch < batchCount; ++batch)
            {
                if (!batchManager->sectionHasCandidates(batch, shaderGroup)) continue;

                vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, shaderGroup);
                vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, shaderGroup);

                MeshShaderPushConstants pushConstants{};
                pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, shaderGroup) * commandsPerSection;

                pushConstants.viewMode = culling.currentViewMode;
                if (culling.meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
                if (culling.meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
                if (culling.meshletOcclusionCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_OCCLUSION_BIT;
                pushConstants.screenWidth = dispatchWidth;
                pushConstants.screenHeight = dispatchHeight;
                pushConstants.hiZMipLevels = prepassHiZMipLevels;
                // Previous-frame VP for motion-vector output (used when MOTION_VECTORS_ENABLED, e.g. DLSS).
                pushConstants.prevViewProjection = cameraBuffer->getData().prevViewProjection;

                cmd.pushConstants(
                    layout,
                    vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT |
                    vk::ShaderStageFlagBits::eFragment,
                    0,
                    sizeof(MeshShaderPushConstants),
                    &pushConstants);

                render::FrameDrawStats::count(render::DrawCategory::Meshes);
                cmd.drawMeshTasksIndirectCountEXT(
                    batchManager->getCombinedDrawCommandBuffer(),
                    cmdOffset,
                    batchManager->getCombinedDrawCountBuffer(),
                    countOffset,
                    commandsPerSection,
                    sizeof(MeshTasksIndirectCommand));
            }
        }
    }

    void GPUDrivenRenderer::renderTransparentDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                                   uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!initialized || !enabled || stats.totalObjects == 0 || !transparentMeshShaderPipeline)
        {
            return;
        }

        uint32_t batchCount = batchManager->getBatchCount();
        uint32_t commandsPerSection = batchManager->getCommandsPerSection();

        if (SHADER_GROUP_TRANSPARENT >= batchManager->getShaderGroupCount())
        {
            return;
        }

        vk::Pipeline activePipeline = transparentMeshShaderPipeline->getPipeline();
        vk::PipelineLayout layout = transparentMeshShaderPipeline->getPipelineLayout();

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, activePipeline);

        auto descriptorSets = buildDescriptorSets(
            iblDescriptorSet, *transparentMeshShaderPipeline, *bindlessTextures, *boneMatrixManager);

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            layout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        bindCausticDescriptorSet(cmd, layout, *transparentMeshShaderPipeline);
        bindRTShadowMaskDescriptorSet(cmd, layout, *transparentMeshShaderPipeline, currentImageIndex);
        bindWorldMaskDescriptorSet(cmd, layout, *transparentMeshShaderPipeline);

        float dispatchWidth, dispatchHeight;
        if (screenWidth > 0 && screenHeight > 0)
        {
            dispatchWidth = static_cast<float>(screenWidth);
            dispatchHeight = static_cast<float>(screenHeight);
        }
        else
        {
            auto extent = swapChain.getSwapchainExtent();
            dispatchWidth = static_cast<float>(extent.width);
            dispatchHeight = static_cast<float>(extent.height);
        }

        for (uint32_t batch = 0; batch < batchCount; ++batch)
        {
            if (!batchManager->sectionHasCandidates(batch, SHADER_GROUP_TRANSPARENT)) continue;

            vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, SHADER_GROUP_TRANSPARENT);
            vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, SHADER_GROUP_TRANSPARENT);

            MeshShaderPushConstants pushConstants{};
            pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, SHADER_GROUP_TRANSPARENT) * commandsPerSection;

            pushConstants.viewMode = culling.currentViewMode;
            if (culling.meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
            if (culling.meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
            pushConstants.screenWidth = dispatchWidth;
            pushConstants.screenHeight = dispatchHeight;
            // Previous-frame VP for motion-vector output (used when MOTION_VECTORS_ENABLED, e.g. DLSS).
            pushConstants.prevViewProjection = cameraBuffer->getData().prevViewProjection;

            cmd.pushConstants(
                layout,
                vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT |
                vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(MeshShaderPushConstants),
                &pushConstants);

            cmd.drawMeshTasksIndirectCountEXT(
                batchManager->getCombinedDrawCommandBuffer(),
                cmdOffset,
                batchManager->getCombinedDrawCountBuffer(),
                countOffset,
                commandsPerSection,
                sizeof(MeshTasksIndirectCommand));
        }
    }

    void GPUDrivenRenderer::renderWBOITDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                              uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!initialized || !enabled || stats.totalObjects == 0 || !wboitMeshShaderPipeline)
        {
            return;
        }

        uint32_t batchCount = batchManager->getBatchCount();
        uint32_t commandsPerSection = batchManager->getCommandsPerSection();

        if (SHADER_GROUP_TRANSPARENT >= batchManager->getShaderGroupCount())
        {
            return;
        }

        vk::Pipeline activePipeline = wboitMeshShaderPipeline->getPipeline();
        vk::PipelineLayout layout = wboitMeshShaderPipeline->getPipelineLayout();

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, activePipeline);

        auto descriptorSets = buildDescriptorSets(
            iblDescriptorSet, *wboitMeshShaderPipeline, *bindlessTextures, *boneMatrixManager);

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            layout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        bindCausticDescriptorSet(cmd, layout, *wboitMeshShaderPipeline);
        bindRTShadowMaskDescriptorSet(cmd, layout, *wboitMeshShaderPipeline, currentImageIndex);
        bindWorldMaskDescriptorSet(cmd, layout, *wboitMeshShaderPipeline);

        float dispatchWidth, dispatchHeight;
        if (screenWidth > 0 && screenHeight > 0)
        {
            dispatchWidth = static_cast<float>(screenWidth);
            dispatchHeight = static_cast<float>(screenHeight);
        }
        else
        {
            auto extent = swapChain.getSwapchainExtent();
            dispatchWidth = static_cast<float>(extent.width);
            dispatchHeight = static_cast<float>(extent.height);
        }

        for (uint32_t batch = 0; batch < batchCount; ++batch)
        {
            if (!batchManager->sectionHasCandidates(batch, SHADER_GROUP_TRANSPARENT)) continue;

            vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, SHADER_GROUP_TRANSPARENT);
            vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, SHADER_GROUP_TRANSPARENT);

            MeshShaderPushConstants pushConstants{};
            pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, SHADER_GROUP_TRANSPARENT) * commandsPerSection;

            pushConstants.viewMode = culling.currentViewMode;
            if (culling.meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
            if (culling.meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
            pushConstants.screenWidth = dispatchWidth;
            pushConstants.screenHeight = dispatchHeight;
            // Previous-frame VP for motion-vector output (used when MOTION_VECTORS_ENABLED, e.g. DLSS).
            pushConstants.prevViewProjection = cameraBuffer->getData().prevViewProjection;

            cmd.pushConstants(
                layout,
                vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT |
                vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(MeshShaderPushConstants),
                &pushConstants);

            cmd.drawMeshTasksIndirectCountEXT(
                batchManager->getCombinedDrawCommandBuffer(),
                cmdOffset,
                batchManager->getCombinedDrawCountBuffer(),
                countOffset,
                commandsPerSection,
                sizeof(MeshTasksIndirectCommand));
        }
    }

    void GPUDrivenRenderer::renderBlendDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                              uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!initialized || !enabled || stats.totalObjects == 0 || !transparentMeshShaderPipeline)
        {
            return;
        }

        uint32_t batchCount = batchManager->getBatchCount();
        uint32_t commandsPerSection = batchManager->getCommandsPerSection();

        if (SHADER_GROUP_BLEND >= batchManager->getShaderGroupCount())
        {
            return;
        }

        vk::Pipeline activePipeline = transparentMeshShaderPipeline->getPipeline();
        vk::PipelineLayout layout = transparentMeshShaderPipeline->getPipelineLayout();

        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, activePipeline);

        auto descriptorSets = buildDescriptorSets(
            iblDescriptorSet, *transparentMeshShaderPipeline, *bindlessTextures, *boneMatrixManager);

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            layout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        bindCausticDescriptorSet(cmd, layout, *transparentMeshShaderPipeline);
        bindRTShadowMaskDescriptorSet(cmd, layout, *transparentMeshShaderPipeline, currentImageIndex);
        bindWorldMaskDescriptorSet(cmd, layout, *transparentMeshShaderPipeline);

        float dispatchWidth, dispatchHeight;
        if (screenWidth > 0 && screenHeight > 0)
        {
            dispatchWidth = static_cast<float>(screenWidth);
            dispatchHeight = static_cast<float>(screenHeight);
        }
        else
        {
            auto extent = swapChain.getSwapchainExtent();
            dispatchWidth = static_cast<float>(extent.width);
            dispatchHeight = static_cast<float>(extent.height);
        }

        for (uint32_t batch = 0; batch < batchCount; ++batch)
        {
            if (!batchManager->sectionHasCandidates(batch, SHADER_GROUP_BLEND)) continue;

            vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, SHADER_GROUP_BLEND);
            vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, SHADER_GROUP_BLEND);

            MeshShaderPushConstants pushConstants{};
            pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, SHADER_GROUP_BLEND) * commandsPerSection;

            pushConstants.viewMode = culling.currentViewMode;
            if (culling.meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
            if (culling.meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
            pushConstants.screenWidth = dispatchWidth;
            pushConstants.screenHeight = dispatchHeight;
            // Previous-frame VP for motion-vector output (used when MOTION_VECTORS_ENABLED, e.g. DLSS).
            pushConstants.prevViewProjection = cameraBuffer->getData().prevViewProjection;

            cmd.pushConstants(
                layout,
                vk::ShaderStageFlagBits::eTaskEXT | vk::ShaderStageFlagBits::eMeshEXT |
                vk::ShaderStageFlagBits::eFragment,
                0,
                sizeof(MeshShaderPushConstants),
                &pushConstants);

            cmd.drawMeshTasksIndirectCountEXT(
                batchManager->getCombinedDrawCommandBuffer(),
                cmdOffset,
                batchManager->getCombinedDrawCountBuffer(),
                countOffset,
                commandsPerSection,
                sizeof(MeshTasksIndirectCommand));
        }
    }

    void GPUDrivenRenderer::renderGIDebug(vk::CommandBuffer cmd, const glm::mat4& viewProjection)
    {
        if (!giDebugRenderer || !giCascadeManager || !giCascadeManager->isInitialized())
        {
            return;
        }

        auto* storage = giCascadeManager->getProbeStorage();
        if (!storage || !storage->isInitialized())
        {
            return;
        }

        giDebugRenderer->render(cmd,
                                 storage->getProbeDataDescSet(),
                                 giCascadeManager->getCascadeInfoDescSet(),
                                 viewProjection,
                                 giCascadeManager->getTotalProbeCount());
    }
}
