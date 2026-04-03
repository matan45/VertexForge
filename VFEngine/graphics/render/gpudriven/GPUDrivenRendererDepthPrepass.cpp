#include "GPUDrivenRenderer.hpp"
#include "../occlusion/DepthPrepass.hpp"
#include "../occlusion/DepthPrepassPipeline.hpp"
#include "../occlusion/HiZBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "print/Log.hpp"

namespace render::gpudriven
{
    void GPUDrivenRenderer::updateAllPipelinesHiZ()
    {
        auto hiZView = prepassHiZ->getHiZImageView();
        auto hiZSampler = prepassHiZ->getHiZSampler();

        if (meshShaderPipeline)
            meshShaderPipeline->updateHiZDescriptor(hiZView, hiZSampler);
        if (transparentMeshShaderPipeline)
            transparentMeshShaderPipeline->updateHiZDescriptor(hiZView, hiZSampler);
        if (terrain.pipeline)
        {
            terrain.pipeline->updateHiZDescriptor(hiZView, hiZSampler);
            terrain.pipeline->setHiZMipLevels(prepassHiZMipLevels);
        }
    }

    void GPUDrivenRenderer::initDepthPrepass()
    {
        if (depthPrepass && depthPrepass->isInitialized())
            return;

        if (!meshShaderPipeline)
        {
            vfLogWarning("Cannot init depth prepass: mesh shader pipeline not ready");
            return;
        }

        auto extent = swapChain.getSwapchainExtent();

        depthPrepass = std::make_unique<occlusion::DepthPrepass>(device, swapChain);
        depthPrepass->init(extent.width, extent.height);

        prepassHiZ = std::make_unique<occlusion::HiZBuffer>(device, swapChain);
        prepassHiZ->init(depthPrepass->getDepthImage(), depthPrepass->getDepthImageView(),
                         vk::Format::eD32Sfloat);
        prepassHiZMipLevels = prepassHiZ->getMipLevels();

        depthPrepassPipeline = std::make_unique<occlusion::DepthPrepassPipeline>(device, swapChain);
        occlusion::DepthPrepassInitInfo info{
            .renderPass = depthPrepass->getRenderPass(),
            .cameraLayout = cachedIBLLayout,
            .perDrawLayout = meshShaderPipeline->getPerDrawDataLayout(),
            .bindlessTextureLayout = bindlessTextures ? bindlessTextures->getDescriptorSetLayout() : meshShaderPipeline->getMeshletDataLayout(),
            .meshletDataLayout = meshShaderPipeline->getMeshletDataLayout(),
            .vertexDataLayout = meshShaderPipeline->getVertexDataLayout(),
            .boneMatrixLayout = boneMatrixManager ? boneMatrixManager->getDescriptorSetLayout() : meshShaderPipeline->getVertexDataLayout(),
            .terrainDataLayout = terrain.pipeline ? terrain.pipeline->getTerrainDataLayout() : meshShaderPipeline->getMeshletDataLayout()
        };
        depthPrepassPipeline->init(info);

        updateAllPipelinesHiZ();

        vfLogInfo("Depth prepass initialized with {} Hi-Z mip levels", prepassHiZMipLevels);
    }

    void GPUDrivenRenderer::renderDepthPrepass(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
    {
        if (!depthPrepass || !depthPrepass->isInitialized())
            initDepthPrepass();

        if (!depthPrepass || !depthPrepass->isInitialized() ||
            !depthPrepassPipeline || !depthPrepassPipeline->isInitialized())
            return;

        auto extent = swapChain.getSwapchainExtent();
        depthPrepass->beginPass(cmd);

        // Render opaque scene meshes first (buildings, walls occlude terrain behind them)
        if (initialized && enabled && stats.totalObjects > 0 &&
            meshShaderPipeline && batchManager && bindlessTextures && boneMatrixManager)
        {
            depthPrepassPipeline->bindScenePipeline(cmd);

            auto scenePipelineLayout = depthPrepassPipeline->getScenePipelineLayout();

            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   scenePipelineLayout, 0, iblDescriptorSet, {});

            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   scenePipelineLayout, 1,
                                   meshShaderPipeline->getPerDrawDataDescriptorSet(), {});

            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   scenePipelineLayout, 2,
                                   bindlessTextures->getDescriptorSet(), {});

            std::array<vk::DescriptorSet, 2> meshletVertexSets = {
                meshShaderPipeline->getMeshletDataDescriptorSet(),
                meshShaderPipeline->getVertexDataDescriptorSet()
            };
            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   scenePipelineLayout, 3, meshletVertexSets, {});

            cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                   scenePipelineLayout, 5,
                                   boneMatrixManager->getDescriptorSet(), {});

            uint32_t batchCount = batchManager->getBatchCount();
            uint32_t commandsPerSection = batchManager->getCommandsPerSection();

            for (uint32_t shaderGroup = 0; shaderGroup <= 2; ++shaderGroup)
            {
                for (uint32_t batch = 0; batch < batchCount; ++batch)
                {
                    vk::DeviceSize cmdOffset = batchManager->getDrawCommandOffset(batch, shaderGroup);
                    vk::DeviceSize countOffset = batchManager->getDrawCountOffset(batch, shaderGroup);

                    occlusion::DepthPrepassPushConstants scenePC{};
                    scenePC.baseDrawIndex = batchManager->getSectionIndex(batch, shaderGroup) * commandsPerSection;
                    scenePC.viewMode = MESHLET_CULL_FRUSTUM_BIT;
                    scenePC.screenWidth = static_cast<float>(extent.width);
                    scenePC.screenHeight = static_cast<float>(extent.height);

                    depthPrepassPipeline->pushSceneConstants(cmd, scenePC);

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

        // Render terrain tiles
        if (terrain.pipeline && terrain.pipeline->getCurrentTileCount() > 0 && terrain.renderingEnabled)
        {
            auto meshletDescSet = terrain.pipeline->getTerrainMeshletDescriptorSet();
            auto vertexDescSet = terrain.pipeline->getTerrainVertexDescriptorSet();
            auto tileDataDescSet = terrain.pipeline->getTerrainDataDescriptorSet();

            if (meshletDescSet && tileDataDescSet)
            {
                depthPrepassPipeline->bindTerrainPipeline(cmd);

                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       depthPrepassPipeline->getTerrainPipelineLayout(),
                                       0, iblDescriptorSet, {});

                std::array<vk::DescriptorSet, 2> terrainMeshletVertexSets = {meshletDescSet, vertexDescSet};
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       depthPrepassPipeline->getTerrainPipelineLayout(),
                                       3, terrainMeshletVertexSets, {});

                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       depthPrepassPipeline->getTerrainPipelineLayout(),
                                       11, tileDataDescSet, {});

                occlusion::TerrainDepthPrepassPushConstants terrainPC{};
                terrainPC.tileCount = terrain.pipeline->getCurrentTileCount();
                terrainPC.viewMode = TERRAIN_CULL_FRUSTUM_BIT;
                terrainPC.screenWidth = static_cast<float>(extent.width);
                terrainPC.screenHeight = static_cast<float>(extent.height);

                depthPrepassPipeline->pushTerrainConstants(cmd, terrainPC);
                cmd.drawMeshTasksEXT(terrainPC.tileCount, 1, 1);
            }
        }

        depthPrepass->endPass(cmd);
    }

    void GPUDrivenRenderer::generatePrepassHiZ(vk::CommandBuffer cmd)
    {
        if (!prepassHiZ || !prepassHiZ->isInitialized())
            return;

        prepassHiZ->generate(cmd);
    }
}
