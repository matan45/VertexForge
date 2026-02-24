#include "GPUDrivenRenderer.hpp"
#include "../../core/SwapChain.hpp"
#include <array>

namespace render::gpudriven
{
    void GPUDrivenRenderer::renderDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
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

        std::array<vk::DescriptorSet, 11> descriptorSets = {
            iblDescriptorSet,
            meshShaderPipeline->getPerDrawDataDescriptorSet(),
            bindlessTextures->getDescriptorSet(),
            meshShaderPipeline->getMeshletDataDescriptorSet(),
            meshShaderPipeline->getVertexDataDescriptorSet(),
            boneMatrixManager->getDescriptorSet(),
            meshShaderPipeline->getLightDataDescriptorSet(),
            meshShaderPipeline->getClusterGridDescriptorSet(),
            meshShaderPipeline->getCullingOutputDescriptorSet(),
            meshShaderPipeline->getShadowDataDescriptorSet(),
            meshShaderPipeline->getShadowTextureDescriptorSet()
        };

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            layout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        auto extent = swapChain.getSwapchainExtent();

        for (uint32_t shaderGroup = 0; shaderGroup <= 2; ++shaderGroup)
        {
            for (uint32_t batch = 0; batch < batchCount; ++batch)
            {
                vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, shaderGroup);
                vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, shaderGroup);

                MeshShaderPushConstants pushConstants{};
                pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, shaderGroup) * commandsPerSection;

                pushConstants.viewMode = currentViewMode;
                if (meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
                if (meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
                pushConstants.screenWidth = static_cast<float>(extent.width);
                pushConstants.screenHeight = static_cast<float>(extent.height);

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

    void GPUDrivenRenderer::renderTransparentDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
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

        std::array<vk::DescriptorSet, 11> descriptorSets = {
            iblDescriptorSet,
            transparentMeshShaderPipeline->getPerDrawDataDescriptorSet(),
            bindlessTextures->getDescriptorSet(),
            transparentMeshShaderPipeline->getMeshletDataDescriptorSet(),
            transparentMeshShaderPipeline->getVertexDataDescriptorSet(),
            boneMatrixManager->getDescriptorSet(),
            transparentMeshShaderPipeline->getLightDataDescriptorSet(),
            transparentMeshShaderPipeline->getClusterGridDescriptorSet(),
            transparentMeshShaderPipeline->getCullingOutputDescriptorSet(),
            transparentMeshShaderPipeline->getShadowDataDescriptorSet(),
            transparentMeshShaderPipeline->getShadowTextureDescriptorSet()
        };

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            layout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        auto extent = swapChain.getSwapchainExtent();

        for (uint32_t batch = 0; batch < batchCount; ++batch)
        {
            vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, SHADER_GROUP_TRANSPARENT);
            vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, SHADER_GROUP_TRANSPARENT);

            MeshShaderPushConstants pushConstants{};
            pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, SHADER_GROUP_TRANSPARENT) * commandsPerSection;

            pushConstants.viewMode = currentViewMode;
            if (meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
            if (meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
            pushConstants.screenWidth = static_cast<float>(extent.width);
            pushConstants.screenHeight = static_cast<float>(extent.height);

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

    void GPUDrivenRenderer::renderWBOITDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
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

        std::array<vk::DescriptorSet, 11> descriptorSets = {
            iblDescriptorSet,
            wboitMeshShaderPipeline->getPerDrawDataDescriptorSet(),
            bindlessTextures->getDescriptorSet(),
            wboitMeshShaderPipeline->getMeshletDataDescriptorSet(),
            wboitMeshShaderPipeline->getVertexDataDescriptorSet(),
            boneMatrixManager->getDescriptorSet(),
            wboitMeshShaderPipeline->getLightDataDescriptorSet(),
            wboitMeshShaderPipeline->getClusterGridDescriptorSet(),
            wboitMeshShaderPipeline->getCullingOutputDescriptorSet(),
            wboitMeshShaderPipeline->getShadowDataDescriptorSet(),
            wboitMeshShaderPipeline->getShadowTextureDescriptorSet()
        };

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            layout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        auto extent = swapChain.getSwapchainExtent();

        for (uint32_t batch = 0; batch < batchCount; ++batch)
        {
            vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, SHADER_GROUP_TRANSPARENT);
            vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, SHADER_GROUP_TRANSPARENT);

            MeshShaderPushConstants pushConstants{};
            pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, SHADER_GROUP_TRANSPARENT) * commandsPerSection;

            pushConstants.viewMode = currentViewMode;
            if (meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
            if (meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
            pushConstants.screenWidth = static_cast<float>(extent.width);
            pushConstants.screenHeight = static_cast<float>(extent.height);

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

    void GPUDrivenRenderer::renderBlendDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
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

        std::array<vk::DescriptorSet, 11> descriptorSets = {
            iblDescriptorSet,
            transparentMeshShaderPipeline->getPerDrawDataDescriptorSet(),
            bindlessTextures->getDescriptorSet(),
            transparentMeshShaderPipeline->getMeshletDataDescriptorSet(),
            transparentMeshShaderPipeline->getVertexDataDescriptorSet(),
            boneMatrixManager->getDescriptorSet(),
            transparentMeshShaderPipeline->getLightDataDescriptorSet(),
            transparentMeshShaderPipeline->getClusterGridDescriptorSet(),
            transparentMeshShaderPipeline->getCullingOutputDescriptorSet(),
            transparentMeshShaderPipeline->getShadowDataDescriptorSet(),
            transparentMeshShaderPipeline->getShadowTextureDescriptorSet()
        };

        cmd.bindDescriptorSets(
            vk::PipelineBindPoint::eGraphics,
            layout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0, nullptr);

        auto extent = swapChain.getSwapchainExtent();

        for (uint32_t batch = 0; batch < batchCount; ++batch)
        {
            vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, SHADER_GROUP_BLEND);
            vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, SHADER_GROUP_BLEND);

            MeshShaderPushConstants pushConstants{};
            pushConstants.baseDrawIndex = batchManager->getSectionIndex(batch, SHADER_GROUP_BLEND) * commandsPerSection;

            pushConstants.viewMode = currentViewMode;
            if (meshletFrustumCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_FRUSTUM_BIT;
            if (meshletBackfaceCullingEnabled) pushConstants.viewMode |= MESHLET_CULL_BACKFACE_BIT;
            pushConstants.screenWidth = static_cast<float>(extent.width);
            pushConstants.screenHeight = static_cast<float>(extent.height);

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
