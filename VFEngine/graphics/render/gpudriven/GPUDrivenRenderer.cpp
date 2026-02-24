#include "GPUDrivenRenderer.hpp"
#include "../mesh/MeshStreamManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/RenderManager.hpp"
#include "material/MaterialInstanceTypes.hpp"
#include "material/MaterialManager.hpp"
#include "print/Logger.hpp"
#include <algorithm>

namespace render::gpudriven
{
    GPUDrivenRenderer::GPUDrivenRenderer(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain)
    {
    }

    GPUDrivenRenderer::~GPUDrivenRenderer()
    {
        cleanup();
    }

    void GPUDrivenRenderer::init(vk::DescriptorSetLayout iblDescriptorSetLayout, vk::RenderPass renderPass)
    {
        if (initialized)
        {
            return;
        }

        loggerInfo("GPUDrivenRenderer: Initializing...");

        cachedIBLLayout = iblDescriptorSetLayout;
        cachedRenderPass = renderPass;

        mergedBuffer = std::make_unique<MergedMeshBuffer>(device);
        mergedBuffer->init();

        if (meshStreamingEnabled)
        {
            meshStreamManager = std::make_unique<mesh::MeshStreamManager>(device, *mergedBuffer);
            loggerInfo("GPUDrivenRenderer: Mesh streaming enabled by default");
        }

        batchManager = std::make_unique<IndirectBatchManager>(device);
        if (!batchManager->initWithAutoConfig())
        {
            loggerError("GPUDrivenRenderer: Failed to initialize batch manager - GPU memory allocation failed");
            if (!batchManager->init(2, 50000, 8))
            {
                loggerError(
                    "GPUDrivenRenderer: Even minimal batch configuration failed - GPU-driven rendering unavailable");
                batchManager.reset();
            }
        }

        bindlessTextures = std::make_unique<BindlessTextureManager>(device);
        bindlessTextures->init();

        cullPipeline = std::make_unique<GPUCullLODPipeline>(device);
        cullPipeline->init();

        cameraBuffer = std::make_unique<GPUDrivenCameraBuffer>(device, swapChain);
        cameraBuffer->init();

        const auto& meshCaps = device.getMeshShaderCapabilities();
        meshShaderSupported = meshCaps.meshShaderSupported && meshCaps.taskShaderSupported;

        if (meshShaderSupported &&
            (MESHLET_MAX_VERTICES > meshCaps.maxMeshOutputVertices ||
                MESHLET_MAX_PRIMITIVES > meshCaps.maxMeshOutputPrimitives))
        {
            loggerError(
                "GPUDrivenRenderer: Meshlet constants ({} vertices, {} primitives) exceed device limits ({}, {})",
                MESHLET_MAX_VERTICES, MESHLET_MAX_PRIMITIVES,
                meshCaps.maxMeshOutputVertices, meshCaps.maxMeshOutputPrimitives);
            meshShaderSupported = false;
        }

        if (meshShaderSupported)
        {
            loggerInfo("GPUDrivenRenderer: Mesh shader supported - using Task+Mesh shader pipeline");

            meshletBuffer = std::make_unique<MeshletBuffer>(device);
            meshletBuffer->init();

            boneMatrixManager = std::make_unique<BoneMatrixManager>(device);
            boneMatrixManager->init();

            lightBufferManager = std::make_unique<lighting::GPULightBufferManager>(device);
            lightBufferManager->init();

            clusterGridManager = std::make_unique<lighting::ClusterGridManager>(device);
            clusterGridManager->init();

            lightCullingPipeline = std::make_unique<lighting::LightCullingPipeline>(device);
            lightCullingPipeline->init(
                clusterGridManager->getTotalClusters(),
                clusterGridManager->getDescriptorSetLayout(),
                lightBufferManager->getDescriptorSetLayout()
            );

            shadowSystem = std::make_unique<shadow::ShadowSystem>(device);
            shadowSystem->init();
            shadowSystem->setLightBufferManager(lightBufferManager.get());

            if (!shadowSystem || !shadowSystem->isInitialized())
            {
                loggerError("GPUDrivenRenderer: Shadow system initialization failed");
                return;
            }

            lightBufferManager->setShadowSystem(shadowSystem.get());

            if (core::RenderManager::getGlobalDeletionQueue())
            {
                shadowSystem->setDeletionQueue(core::RenderManager::getGlobalDeletionQueue());
            }

            MeshPipelineInitInfo pipelineInfo{
                .iblLayout = iblDescriptorSetLayout,
                .bindlessTextureLayout = bindlessTextures->getDescriptorSetLayout(),
                .boneMatrixLayout = boneMatrixManager->getDescriptorSetLayout(),
                .lightDataLayout = lightBufferManager->getDescriptorSetLayout(),
                .clusterGridLayout = clusterGridManager->getDescriptorSetLayout(),
                .cullingOutputLayout = lightCullingPipeline->getDescriptorSetLayout(),
                .shadowDataLayout = shadowSystem->getShadowDataLayout(),
                .shadowTextureLayout = shadowSystem->getShadowTextureLayout(),
                .renderPass = renderPass
            };

            meshShaderPipeline = std::make_unique<MeshShaderPipeline>(device, swapChain);
            meshShaderPipeline->init(pipelineInfo);

            pipelineInfo.transparentMode = true;
            transparentMeshShaderPipeline = std::make_unique<MeshShaderPipeline>(device, swapChain);
            transparentMeshShaderPipeline->init(pipelineInfo);

            shadowSystem->initShadowPass(
                meshShaderPipeline->getPerDrawDataLayout(),
                meshShaderPipeline->getMeshletDataLayout(),
                meshShaderPipeline->getVertexDataLayout(),
                boneMatrixManager->getDescriptorSetLayout()
            );

            if (meshStreamManager)
            {
                meshStreamManager->setMeshletBuffer(meshletBuffer.get());
                loggerInfo("GPUDrivenRenderer: Meshlet streaming enabled");
            }

            initTerrainSubsystems(iblDescriptorSetLayout, renderPass);
            initWaterSubsystems(iblDescriptorSetLayout, renderPass);
        }
        else
        {
            loggerError(
                "GPUDrivenRenderer: Mesh shaders not supported - GPU-driven rendering requires mesh shader support");
            loggerError("GPUDrivenRenderer: The VK_EXT_mesh_shader extension with task shader support is required");
            return;
        }

        if (!materialChangeCallbackId)
        {
            materialChangeCallbackId = material::MaterialManager::instance().registerChangeCallback(
                [this](const std::string& materialPath) {
                    pbrCache.erase(materialPath);
                    registeredMaterialPaths.erase(materialPath);

                    if (!material::isInstanceFile(materialPath))
                    {
                        std::erase_if(pbrCache, [](const auto& pair) {
                            return material::isInstanceFile(pair.first);
                        });
                        std::erase_if(registeredMaterialPaths, [](const std::string& path) {
                            return material::isInstanceFile(path);
                        });
                    }
                });
        }

        initialized = true;
        loggerInfo("GPUDrivenRenderer: Initialized successfully");
    }

    void GPUDrivenRenderer::initWBOITPipeline(vk::RenderPass wboitRenderPass)
    {
        if (!initialized || !meshShaderSupported || !wboitRenderPass) return;

        cachedWBOITRenderPass = wboitRenderPass;

        wboitMeshShaderPipeline = std::make_unique<MeshShaderPipeline>(device, swapChain);
        wboitMeshShaderPipeline->init({
            .iblLayout = cachedIBLLayout,
            .bindlessTextureLayout = bindlessTextures->getDescriptorSetLayout(),
            .boneMatrixLayout = boneMatrixManager->getDescriptorSetLayout(),
            .lightDataLayout = lightBufferManager->getDescriptorSetLayout(),
            .clusterGridLayout = clusterGridManager->getDescriptorSetLayout(),
            .cullingOutputLayout = lightCullingPipeline->getDescriptorSetLayout(),
            .shadowDataLayout = shadowSystem->getShadowDataLayout(),
            .shadowTextureLayout = shadowSystem->getShadowTextureLayout(),
            .renderPass = wboitRenderPass,
            .wboitMode = true
        });

        loggerInfo("GPUDrivenRenderer: WBOIT mesh shader pipeline initialized");
    }

    void GPUDrivenRenderer::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        if (materialChangeCallbackId)
        {
            material::MaterialManager::instance().unregisterChangeCallback(materialChangeCallbackId);
            materialChangeCallbackId = {};
        }

        pbrCache.clear();
        registeredMaterialPaths.clear();

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        if (volumetricPipeline) volumetricPipeline->cleanup();
        if (waterPipeline) waterPipeline->cleanup();
        if (waterMeshBuffer) waterMeshBuffer->cleanup();
        if (terrainPipeline) terrainPipeline->cleanup();
        if (terrainMeshBuffer) terrainMeshBuffer->cleanup();
        if (lightOcclusionCulling) lightOcclusionCulling->cleanup();
        if (wboitMeshShaderPipeline) wboitMeshShaderPipeline->cleanup();
        if (transparentMeshShaderPipeline) transparentMeshShaderPipeline->cleanup();
        if (meshShaderPipeline) meshShaderPipeline->cleanup();
        if (shadowSystem) shadowSystem->cleanup();
        if (lightCullingPipeline) lightCullingPipeline->cleanup();
        if (clusterGridManager) clusterGridManager->cleanup();
        if (lightBufferManager) lightBufferManager->cleanup();
        if (boneMatrixManager) boneMatrixManager->cleanup();
        if (meshletBuffer) meshletBuffer->cleanup();
        if (cameraBuffer) cameraBuffer->cleanup();
        if (cullPipeline) cullPipeline->cleanup();
        if (bindlessTextures) bindlessTextures->cleanup();
        if (batchManager) batchManager->cleanup();
        if (mergedBuffer) mergedBuffer->cleanup();

        volumetricPipeline.reset();
        meshStreamManager.reset();
        terrainStreamManager.reset();
        terrainAdapter.reset();
        terrainPipeline.reset();
        terrainMeshBuffer.reset();
        waterPipeline.reset();
        waterMeshBuffer.reset();
        lightOcclusionCulling.reset();
        wboitMeshShaderPipeline.reset();
        transparentMeshShaderPipeline.reset();
        meshShaderPipeline.reset();
        shadowSystem.reset();
        lightCullingPipeline.reset();
        clusterGridManager.reset();
        lightBufferManager.reset();
        boneMatrixManager.reset();
        meshletBuffer.reset();
        cameraBuffer.reset();
        cullPipeline.reset();
        bindlessTextures.reset();
        batchManager.reset();
        mergedBuffer.reset();

        initialized = false;
        loggerInfo("GPUDrivenRenderer: Cleaned up");
    }

    void GPUDrivenRenderer::updateRenderPass(vk::RenderPass newRenderPass, vk::DescriptorSetLayout newIBLLayout)
    {
        if (!initialized) return;

        bool renderPassChanged = (cachedRenderPass != newRenderPass);
        bool iblLayoutChanged = (newIBLLayout && cachedIBLLayout != newIBLLayout);

        if (!renderPassChanged && !iblLayoutChanged) return;

        loggerInfo("GPUDrivenRenderer: Updating render pass/IBL layout, recreating pipelines");

        cachedRenderPass = newRenderPass;
        if (newIBLLayout)
        {
            cachedIBLLayout = newIBLLayout;
        }

        bool canRecreate = true;
        if (!meshShaderPipeline)
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: meshShaderPipeline is null");
            canRecreate = false;
        }
        if (!boneMatrixManager)
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: boneMatrixManager is null");
            canRecreate = false;
        }
        if (!lightBufferManager)
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: lightBufferManager is null");
            canRecreate = false;
        }
        if (!clusterGridManager)
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: clusterGridManager is null");
            canRecreate = false;
        }
        if (!lightCullingPipeline)
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: lightCullingPipeline is null");
            canRecreate = false;
        }
        if (!bindlessTextures)
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: bindlessTextures is null");
            canRecreate = false;
        }

        if (canRecreate && shadowSystem)
        {
            MeshPipelineInitInfo pipelineInfo{
                .iblLayout = cachedIBLLayout,
                .bindlessTextureLayout = bindlessTextures->getDescriptorSetLayout(),
                .boneMatrixLayout = boneMatrixManager->getDescriptorSetLayout(),
                .lightDataLayout = lightBufferManager->getDescriptorSetLayout(),
                .clusterGridLayout = clusterGridManager->getDescriptorSetLayout(),
                .cullingOutputLayout = lightCullingPipeline->getDescriptorSetLayout(),
                .shadowDataLayout = shadowSystem->getShadowDataLayout(),
                .shadowTextureLayout = shadowSystem->getShadowTextureLayout(),
                .renderPass = cachedRenderPass
            };

            meshShaderPipeline->recreate(pipelineInfo);

            if (transparentMeshShaderPipeline)
            {
                pipelineInfo.transparentMode = true;
                transparentMeshShaderPipeline->recreate(pipelineInfo);
                pipelineInfo.transparentMode = false;
            }

            if (wboitMeshShaderPipeline && cachedWBOITRenderPass)
            {
                pipelineInfo.renderPass = cachedWBOITRenderPass;
                pipelineInfo.wboitMode = true;
                wboitMeshShaderPipeline->recreate(pipelineInfo);
            }

            if (terrainPipeline)
            {
                terrainPipeline->recreate(cachedIBLLayout,
                                          bindlessTextures->getDescriptorSetLayout(),
                                          meshShaderPipeline->getMeshletDataLayout(),
                                          meshShaderPipeline->getVertexDataLayout(),
                                          lightBufferManager->getDescriptorSetLayout(),
                                          clusterGridManager->getDescriptorSetLayout(),
                                          lightCullingPipeline->getDescriptorSetLayout(),
                                          shadowSystem->getShadowDataLayout(),
                                          shadowSystem->getShadowTextureLayout(),
                                          cachedRenderPass);
            }

            if (waterPipeline)
            {
                waterPipeline->recreate(cachedIBLLayout, cachedRenderPass);
            }
        }
        else
        {
            loggerError("GPUDrivenRenderer::recreatePipelines: Cannot recreate pipeline due to missing components");
        }
    }

}
