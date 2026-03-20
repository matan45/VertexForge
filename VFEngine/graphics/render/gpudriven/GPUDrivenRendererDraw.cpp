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
            vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, SHADER_GROUP_TRANSPARENT);
            vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, SHADER_GROUP_TRANSPARENT);

            MeshShaderPushConstants pushConstants{};
            pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, SHADER_GROUP_TRANSPARENT) * commandsPerSection;

            pushConstants.viewMode = culling.currentViewMode;
            if (culling.meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
            if (culling.meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
            pushConstants.screenWidth = dispatchWidth;
            pushConstants.screenHeight = dispatchHeight;

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
            vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, SHADER_GROUP_TRANSPARENT);
            vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, SHADER_GROUP_TRANSPARENT);

            MeshShaderPushConstants pushConstants{};
            pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, SHADER_GROUP_TRANSPARENT) * commandsPerSection;

            pushConstants.viewMode = culling.currentViewMode;
            if (culling.meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
            if (culling.meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
            pushConstants.screenWidth = dispatchWidth;
            pushConstants.screenHeight = dispatchHeight;

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
            vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, SHADER_GROUP_BLEND);
            vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, SHADER_GROUP_BLEND);

            MeshShaderPushConstants pushConstants{};
            pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, SHADER_GROUP_BLEND) * commandsPerSection;

            pushConstants.viewMode = culling.currentViewMode;
            if (culling.meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
            if (culling.meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
            pushConstants.screenWidth = dispatchWidth;
            pushConstants.screenHeight = dispatchHeight;

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
