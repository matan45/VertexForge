#include "GPUDrivenRenderer.hpp"
#include "../occlusion/DepthPrepass.hpp"
#include "../occlusion/DepthPrepassPipeline.hpp"
#include "../occlusion/HiZBuffer.hpp"
#include "../upscaling/UpscaleManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/ImageUtilities.hpp"
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
            .colorFormats = {occlusion::DepthPrepass::getNormalFormat()},
            .depthFormat = vk::Format::eD32Sfloat,
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

        // VK-1397: produce DLSS-D Ray Reconstruction albedo guides from the prepass only
        // while Ray Reconstruction is the active upscaler. A change rebuilds the small,
        // self-contained prepass pipeline + (de)allocates the albedo targets.
        bool wantAlbedo = false;
        if (auto* um = device.getUpscaleManager())
            wantAlbedo = um->isDLSSRRActive();
        if (wantAlbedo != prepassAlbedoActive)
        {
            depthPrepass->setAlbedoEnabled(wantAlbedo);
            depthPrepassPipeline->setAlbedoMode(wantAlbedo);
            prepassAlbedoActive = wantAlbedo;
        }

        auto extent = swapChain.getSwapchainExtent();

        // The guide targets sit in SHADER_READ between frames (sampled by Streamline at
        // upscale time). Bring them to COLOR_ATTACHMENT for this frame's writes; the pass
        // clears + rewrites them. The normal target doubles as RR's normal-roughness guide.
        if (prepassAlbedoActive)
        {
            core::ImageUtilities::transitionImageLayout(cmd, depthPrepass->getNormalImage(),
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);
            core::ImageUtilities::transitionImageLayout(cmd, depthPrepass->getDiffuseAlbedoImage(),
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);
            core::ImageUtilities::transitionImageLayout(cmd, depthPrepass->getSpecularAlbedoImage(),
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageAspectFlagBits::eColor);
        }

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
                render::FrameDrawStats::count(render::DrawCategory::Meshes);
            }
        }

        depthPrepass->endPass(cmd);

        // Hand the guide targets to Streamline's expected SHADER_READ layout for the
        // upscale evaluate later this frame (VK-1397; also closes the VK-1245 normal
        // -roughness layout gap, since the normal target is now explicitly transitioned).
        if (prepassAlbedoActive)
        {
            core::ImageUtilities::transitionImageLayout(cmd, depthPrepass->getNormalImage(),
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
            core::ImageUtilities::transitionImageLayout(cmd, depthPrepass->getDiffuseAlbedoImage(),
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
            core::ImageUtilities::transitionImageLayout(cmd, depthPrepass->getSpecularAlbedoImage(),
                vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::ImageAspectFlagBits::eColor);
        }
    }

    void GPUDrivenRenderer::generatePrepassHiZ(vk::CommandBuffer cmd)
    {
        if (!prepassHiZ || !prepassHiZ->isInitialized())
            return;

        prepassHiZ->generate(cmd);
    }
}
