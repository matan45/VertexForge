#include "GPUDrivenRenderer.hpp"
#include "../occlusion/DepthPrepass.hpp"
#include "../occlusion/DepthPrepassPipeline.hpp"
#include "../occlusion/HiZBuffer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "print/Log.hpp"

namespace render::gpudriven
{
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

        // Create depth prepass resources
        depthPrepass = std::make_unique<occlusion::DepthPrepass>(device, swapChain);
        depthPrepass->init(extent.width, extent.height);

        // Create prepass Hi-Z buffer (reuses existing HiZBuffer class)
        prepassHiZ = std::make_unique<occlusion::HiZBuffer>(device, swapChain);
        prepassHiZ->init(depthPrepass->getDepthImage(), depthPrepass->getDepthImageView(),
                         vk::Format::eD32Sfloat);
        prepassHiZMipLevels = prepassHiZ->getMipLevels();

        // Create depth prepass rendering pipelines
        // Set 0 in the mesh shader pipeline is the IBL/Camera layout (cachedIBLLayout)
        depthPrepassPipeline = std::make_unique<occlusion::DepthPrepassPipeline>(device, swapChain);

        vk::DescriptorSetLayout perDrawLayout = meshShaderPipeline->getPerDrawDataLayout();
        vk::DescriptorSetLayout meshletLayout = meshShaderPipeline->getMeshletDataLayout();
        vk::DescriptorSetLayout vertexLayout = meshShaderPipeline->getVertexDataLayout();
        vk::DescriptorSetLayout terrainDataLayout = terrain.pipeline ?
            terrain.pipeline->getTerrainDataLayout() : vk::DescriptorSetLayout{};

        vk::DescriptorSetLayout boneLayout = boneMatrixManager ?
            boneMatrixManager->getDescriptorSetLayout() : vertexLayout;
        vk::DescriptorSetLayout bindlessLayout = bindlessTextures ?
            bindlessTextures->getDescriptorSetLayout() : meshletLayout;

        depthPrepassPipeline->init(
            depthPrepass->getRenderPass(),
            cachedIBLLayout,        // set 0 = IBL/Camera (same as main pipeline)
            perDrawLayout,          // set 1 = PerDrawData
            bindlessLayout,         // set 2 = Bindless textures
            meshletLayout,          // set 3 = Meshlet data
            vertexLayout,           // set 4 = Vertex data
            boneLayout,             // set 5 = Bone matrices
            terrainDataLayout ? terrainDataLayout : meshletLayout  // set 11 = Terrain
        );

        // Update Hi-Z descriptors in the main pipelines so task shaders can sample it
        if (meshShaderPipeline)
        {
            meshShaderPipeline->updateHiZDescriptor(
                prepassHiZ->getHiZImageView(),
                prepassHiZ->getHiZSampler());
        }
        if (transparentMeshShaderPipeline)
        {
            transparentMeshShaderPipeline->updateHiZDescriptor(
                prepassHiZ->getHiZImageView(),
                prepassHiZ->getHiZSampler());
        }
        if (terrain.pipeline)
        {
            terrain.pipeline->updateHiZDescriptor(
                prepassHiZ->getHiZImageView(),
                prepassHiZ->getHiZSampler());
            terrain.pipeline->setHiZMipLevels(prepassHiZMipLevels);
        }

        vfLogInfo("Depth prepass initialized with {} Hi-Z mip levels", prepassHiZMipLevels);
    }

    void GPUDrivenRenderer::renderDepthPrepass(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
    {
        // Lazy initialization on first use
        if (!depthPrepass || !depthPrepass->isInitialized())
        {
            initDepthPrepass();
        }

        if (!depthPrepass || !depthPrepass->isInitialized() ||
            !depthPrepassPipeline || !depthPrepassPipeline->isInitialized())
            return;

        auto extent = swapChain.getSwapchainExtent();

        depthPrepass->beginPass(cmd);

        // Render terrain at LOD3 (coarsest) into depth — terrain is the primary occluder
        if (terrain.pipeline && terrain.pipeline->getCurrentTileCount() > 0 && terrain.renderingEnabled)
        {
            depthPrepassPipeline->bindTerrainPipeline(cmd);

            // Bind terrain descriptor sets matching the terrain depth prepass pipeline layout
            // The terrain pipeline already has all descriptors set up — we reuse them
            auto terrainMeshletDescSet = terrain.pipeline->getTerrainMeshletDescriptorSet();
            auto terrainVertexDescSet = terrain.pipeline->getTerrainVertexDescriptorSet();
            auto terrainDataDescSet = terrain.pipeline->getTerrainDataDescriptorSet();

            if (terrainMeshletDescSet && terrainDataDescSet)
            {
                // Set 0 = IBL/Camera UBO (contains CameraData with frustumPlanes, view, projection)
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       depthPrepassPipeline->getTerrainPipelineLayout(),
                                       0, iblDescriptorSet, {});

                // Set 3 = meshlet data, Set 4 = vertex data
                std::array<vk::DescriptorSet, 2> meshletVertexSets = {
                    terrainMeshletDescSet, terrainVertexDescSet
                };
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       depthPrepassPipeline->getTerrainPipelineLayout(),
                                       3, meshletVertexSets, {});

                // Set 11 = terrain tile data
                cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                                       depthPrepassPipeline->getTerrainPipelineLayout(),
                                       11, terrainDataDescSet, {});

                occlusion::TerrainDepthPrepassPushConstants terrainPC{};
                terrainPC.tileCount = terrain.pipeline->getCurrentTileCount();
                terrainPC.viewMode = TERRAIN_CULL_FRUSTUM_BIT;
                terrainPC.screenWidth = static_cast<float>(extent.width);
                terrainPC.screenHeight = static_cast<float>(extent.height);

                depthPrepassPipeline->pushTerrainConstants(cmd, terrainPC);
                cmd.drawMeshTasksEXT(terrainPC.tileCount, 1, 1);
            }
        }

        // NOTE: Scene object depth prepass is omitted for now. Terrain is the primary
        // occluder in outdoor scenes. Scene object depth prepass can be added later by
        // iterating the batch manager's indirect draw commands with the depth-only pipeline.

        depthPrepass->endPass(cmd);
    }

    void GPUDrivenRenderer::generatePrepassHiZ(vk::CommandBuffer cmd)
    {
        if (!prepassHiZ || !prepassHiZ->isInitialized())
            return;

        prepassHiZ->generate(cmd);
    }
}
