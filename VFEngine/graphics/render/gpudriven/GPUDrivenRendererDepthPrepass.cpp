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

        // Initialize SVT feedback pipeline now that depth prepass is available
        if (svt.initialized && svt.feedbackPipeline && !svt.feedbackPipeline->isInitialized())
        {
            // Create a sampler for depth sampling
            vk::SamplerCreateInfo samplerInfo{};
            samplerInfo.magFilter = vk::Filter::eNearest;
            samplerInfo.minFilter = vk::Filter::eNearest;
            samplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
            samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
            samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            auto depthSampler = device.getLogicalDevice().createSampler(samplerInfo);

            svt.feedbackPipeline->init(svt.config, depthPrepass->getDepthImageView(), depthSampler);
        }

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

                std::array<vk::DescriptorSet, 2> meshletVertexSets = {meshletDescSet, vertexDescSet};
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       depthPrepassPipeline->getTerrainPipelineLayout(),
                                       3, meshletVertexSets, {});

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
