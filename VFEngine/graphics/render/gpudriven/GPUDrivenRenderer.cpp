#include "GPUDrivenRenderer.hpp"
#include "../occlusion/DepthPrepass.hpp"
#include "../occlusion/DepthPrepassPipeline.hpp"
#include "../occlusion/HiZBuffer.hpp"
#include "../mesh/MeshStreamManager.hpp"
#include "../vegetation/GrassMeshShaderPipeline.hpp"
#include "../vegetation/WindSystem.hpp"
#include "../vegetation/VegetationBufferManager.hpp"
#include "../vegetation/GrassStreamManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../../core/RenderManager.hpp"
#include "../upscaling/UpscaleManager.hpp"
#include "material/MaterialInstanceTypes.hpp"
#include "material/MaterialManager.hpp"
#include "print/Log.hpp"
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

    void GPUDrivenRenderer::init(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                  const std::vector<vk::Format>& colorFormats, vk::Format depthFormat,
                                  vk::ImageView sceneDepthView)
    {
        if (initialized)
        {
            return;
        }

        cachedIBLLayout = iblDescriptorSetLayout;
        cachedColorFormats = colorFormats;
        cachedDepthFormat = depthFormat;

        mergedBuffer = std::make_unique<MergedMeshBuffer>(device);
        mergedBuffer->init();

        objectStreamManager = std::make_unique<GPUObjectStreamManager>(*mergedBuffer);
        objectStreamManager->init();

        if (meshStreamingEnabled)
        {
            meshStreamManager = std::make_unique<mesh::MeshStreamManager>(device, *mergedBuffer);
        }

        batchManager = std::make_unique<IndirectBatchManager>(device);
        if (!batchManager->initWithAutoConfig())
        {
            vfLogError("GPUDrivenRenderer: Failed to initialize batch manager - GPU memory allocation failed");
            if (!batchManager->init(2, 50000, 8))
            {
                vfLogError(
                    "GPUDrivenRenderer: Even minimal batch configuration failed - GPU-driven rendering unavailable");
                batchManager.reset();
            }
        }

        bindlessTextures = std::make_unique<BindlessTextureManager>(device);
        bindlessTextures->init();

        textureStreamManager = std::make_unique<TextureStreamManager>(device, *bindlessTextures);
        textureStreamManager->init();

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
            vfLogError(
                "GPUDrivenRenderer: Meshlet constants ({} vertices, {} primitives) exceed device limits ({}, {})",
                MESHLET_MAX_VERTICES, MESHLET_MAX_PRIMITIVES,
                meshCaps.maxMeshOutputVertices, meshCaps.maxMeshOutputPrimitives);
            meshShaderSupported = false;
        }

        if (meshShaderSupported)
        {
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
                vfLogError("GPUDrivenRenderer: Shadow system initialization failed");
                return;
            }

            lightBufferManager->setShadowSystem(shadowSystem.get());

            if (core::RenderManager::getGlobalDeletionQueue())
            {
                shadowSystem->setDeletionQueue(core::RenderManager::getGlobalDeletionQueue());
                if (textureStreamManager)
                {
                    textureStreamManager->setDeletionQueue(core::RenderManager::getGlobalDeletionQueue());
                }
            }

            bool mvEnabled = false;
            if (auto* um = device.getUpscaleManager())
                mvEnabled = um->isActive();

            MeshPipelineInitInfo pipelineInfo{
                .iblLayout = iblDescriptorSetLayout,
                .bindlessTextureLayout = bindlessTextures->getDescriptorSetLayout(),
                .boneMatrixLayout = boneMatrixManager->getDescriptorSetLayout(),
                .lightDataLayout = lightBufferManager->getDescriptorSetLayout(),
                .clusterGridLayout = clusterGridManager->getDescriptorSetLayout(),
                .cullingOutputLayout = lightCullingPipeline->getDescriptorSetLayout(),
                .shadowDataLayout = shadowSystem->getShadowDataLayout(),
                .shadowTextureLayout = shadowSystem->getShadowTextureLayout(),
                .colorAttachmentFormats = colorFormats,
                .depthAttachmentFormat = depthFormat,
                .motionVectorsEnabled = mvEnabled
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

            // Tier 4: per-shadow-view GPU culling. Needs the cull pipeline (created above) and
            // the mesh pipeline's set-0 layout (for the shadow-pass perDrawData variant set).
            shadowCullManager = std::make_unique<ShadowCullManager>(device);
            shadowCullManager->init(cullPipeline.get(), meshShaderPipeline->getPerDrawDataLayout());

            if (meshStreamManager)
            {
                meshStreamManager->setMeshletBuffer(meshletBuffer.get());
            }

            initTerrainSubsystems(iblDescriptorSetLayout, colorFormats, depthFormat);
            initWaterSubsystems(iblDescriptorSetLayout, colorFormats, depthFormat, sceneDepthView);
            initVegetationSubsystems(iblDescriptorSetLayout, colorFormats, depthFormat);
            initBillboardSubsystems(iblDescriptorSetLayout, colorFormats, depthFormat);
        }
        else
        {
            vfLogError(
                "GPUDrivenRenderer: Mesh shaders not supported - GPU-driven rendering requires mesh shader support");
            vfLogError("GPUDrivenRenderer: The VK_EXT_mesh_shader extension with task shader support is required");
            return;
        }

        if (!materials.changeCallbackId)
        {
            materials.changeCallbackId = material::MaterialManager::instance().registerChangeCallback(
                [this](const std::string& materialPath) {
                    materials.pbrCache.erase(materialPath);
                    materials.registeredPaths.erase(materialPath);

                    if (!material::isInstanceFile(materialPath))
                    {
                        std::erase_if(materials.pbrCache, [](const auto& pair) {
                            return material::isInstanceFile(pair.first);
                        });
                        std::erase_if(materials.registeredPaths, [](const std::string& path) {
                            return material::isInstanceFile(path);
                        });
                    }
                });
        }

        initialized = true;
    }

    void GPUDrivenRenderer::initWBOITPipeline(const std::vector<vk::Format>& wboitColorFormats, vk::Format wboitDepthFormat)
    {
        if (!initialized || !meshShaderSupported || wboitColorFormats.empty()) return;

        cachedWBOITColorFormats = wboitColorFormats;
        cachedWBOITDepthFormat = wboitDepthFormat;

        vk::DescriptorSetLayout giLayout{};
        if (giCascadeManager)
        {
            auto* storage = giCascadeManager->getProbeStorage();
            if (storage && storage->isInitialized())
                giLayout = storage->getSamplingLayout();
        }

        vk::DescriptorSetLayout wboitCausticLayout{};
        if (water.causticsResources && water.causticsResources->isInitialized())
            wboitCausticLayout = water.causticsResources->getDescriptorSetLayout();

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
            .giProbeDataLayout = giLayout,
            .causticLayout = wboitCausticLayout,
            .worldMaskLayout = currentWorldMaskLayout(),
            .colorAttachmentFormats = wboitColorFormats,
            .depthAttachmentFormat = wboitDepthFormat,
            .wboitMode = true
        });

        if (giLayout && giCascadeManager)
        {
            auto* storage = giCascadeManager->getProbeStorage();
            wboitMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
        }
        if (wboitCausticLayout)
            wboitMeshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());
    }

    void GPUDrivenRenderer::cleanup()
    {
        if (!initialized)
        {
            return;
        }

        if (materials.changeCallbackId)
        {
            material::MaterialManager::instance().unregisterChangeCallback(materials.changeCallbackId);
            materials.changeCallbackId = {};
        }

        materials.pbrCache.clear();
        materials.registeredPaths.clear();

        vk::Device vkDevice = device.getLogicalDevice();
        vkDevice.waitIdle();

        cleanupGI();
        // Safety net: ensure RT shadow resources are cleaned even if GI cleanup path missed them
        if (rtShadowProfiler) { rtShadowProfiler->cleanup(vkDevice); rtShadowProfiler.reset(); }
        if (rtShadowDenoiser) { rtShadowDenoiser->cleanup(); rtShadowDenoiser.reset(); }
        if (rtShadowPipeline) { rtShadowPipeline->cleanup(); rtShadowPipeline.reset(); }
        if (rtSpotShadowDenoiser) { rtSpotShadowDenoiser->cleanup(); rtSpotShadowDenoiser.reset(); }
        if (rtSpotShadowPipeline) { rtSpotShadowPipeline->cleanup(); rtSpotShadowPipeline.reset(); }
        if (accelStructManager) { accelStructManager->cleanup(); accelStructManager.reset(); }
        if (textureStreamManager) textureStreamManager->cleanup();
        if (objectStreamManager) objectStreamManager->cleanup();
        if (lightStreamManager) lightStreamManager->cleanup();
        if (volumetricPipeline) volumetricPipeline->cleanup();
        if (fogVolumeBufferManager) fogVolumeBufferManager->cleanup();
        for (auto& band : water.oceanBands) { if (band) band->cleanup(); }
        if (water.causticsResources) water.causticsResources->cleanup();
        if (water.pipeline) water.pipeline->cleanup();
        if (water.meshBuffer) water.meshBuffer->cleanup();
        if (billboard.meshShaderPipeline) billboard.meshShaderPipeline->cleanup();
        if (billboard.bufferManager) billboard.bufferManager->cleanup();
        if (billboard.streamManager) billboard.streamManager->cleanup();
        cleanupVegetation();
        if (terrain.pipeline) terrain.pipeline->cleanup();
        if (terrain.meshBuffer) terrain.meshBuffer->cleanup();
        if (depthPrepassPipeline) depthPrepassPipeline->cleanup();
        if (prepassHiZ) prepassHiZ->cleanup();
        if (depthPrepass) depthPrepass->cleanup();
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

        billboard.meshShaderPipeline.reset();
        billboard.bufferManager.reset();
        billboard.streamManager.reset();
        billboard.initialized = false;
        volumetricPipeline.reset();
        fogVolumeBufferManager.reset();
        meshStreamManager.reset();
        terrain.streamManager.reset();
        terrain.adapter.reset();
        terrain.pipeline.reset();
        terrain.meshBuffer.reset();
        for (auto& band : water.oceanBands) band.reset();
        // Cleanup composite descriptor resources
        if (water.multiBandOceanPool) { vkDevice.destroyDescriptorPool(water.multiBandOceanPool); water.multiBandOceanPool = nullptr; }
        if (water.multiBandOceanLayout) { vkDevice.destroyDescriptorSetLayout(water.multiBandOceanLayout); water.multiBandOceanLayout = nullptr; }
        water.multiBandOceanDescSet = nullptr;
        water.refractionResources.reset();
        water.pipeline.reset();
        water.meshBuffer.reset();
        depthPrepassPipeline.reset();
        prepassHiZ.reset();
        depthPrepass.reset();
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
    }

    void GPUDrivenRenderer::updateFormats(const std::vector<vk::Format>& colorFormats, vk::Format depthFormat,
                                          vk::DescriptorSetLayout newIBLLayout)
    {
        if (!initialized) return;

        bool formatsChanged = (cachedColorFormats != colorFormats) || (cachedDepthFormat != depthFormat);
        bool iblLayoutChanged = (newIBLLayout && cachedIBLLayout != newIBLLayout);

        if (!formatsChanged && !iblLayoutChanged) return;

        vfLogInfo("GPUDrivenRenderer: Updating formats/IBL layout, recreating pipelines");

        cachedColorFormats = colorFormats;
        cachedDepthFormat = depthFormat;
        if (newIBLLayout)
        {
            cachedIBLLayout = newIBLLayout;
        }

        bool canRecreate = true;
        if (!meshShaderPipeline)
        {
            vfLogError("GPUDrivenRenderer::recreatePipelines: meshShaderPipeline is null");
            canRecreate = false;
        }
        if (!boneMatrixManager)
        {
            vfLogError("GPUDrivenRenderer::recreatePipelines: boneMatrixManager is null");
            canRecreate = false;
        }
        if (!lightBufferManager)
        {
            vfLogError("GPUDrivenRenderer::recreatePipelines: lightBufferManager is null");
            canRecreate = false;
        }
        if (!clusterGridManager)
        {
            vfLogError("GPUDrivenRenderer::recreatePipelines: clusterGridManager is null");
            canRecreate = false;
        }
        if (!lightCullingPipeline)
        {
            vfLogError("GPUDrivenRenderer::recreatePipelines: lightCullingPipeline is null");
            canRecreate = false;
        }
        if (!bindlessTextures)
        {
            vfLogError("GPUDrivenRenderer::recreatePipelines: bindlessTextures is null");
            canRecreate = false;
        }

        if (canRecreate && shadowSystem)
        {
            vk::DescriptorSetLayout giLayout{};
            if (giCascadeManager)
            {
                auto* storage = giCascadeManager->getProbeStorage();
                if (storage && storage->isInitialized())
                    giLayout = storage->getSamplingLayout();
            }

            vk::DescriptorSetLayout causticLayout{};
            if (water.causticsResources && water.causticsResources->isInitialized())
                causticLayout = water.causticsResources->getDescriptorSetLayout();

            bool mvEnabled = false;
            if (auto* um = device.getUpscaleManager())
                mvEnabled = um->isActive();

            MeshPipelineInitInfo pipelineInfo{
                .iblLayout = cachedIBLLayout,
                .bindlessTextureLayout = bindlessTextures->getDescriptorSetLayout(),
                .boneMatrixLayout = boneMatrixManager->getDescriptorSetLayout(),
                .lightDataLayout = lightBufferManager->getDescriptorSetLayout(),
                .clusterGridLayout = clusterGridManager->getDescriptorSetLayout(),
                .cullingOutputLayout = lightCullingPipeline->getDescriptorSetLayout(),
                .shadowDataLayout = shadowSystem->getShadowDataLayout(),
                .shadowTextureLayout = shadowSystem->getShadowTextureLayout(),
                .giProbeDataLayout = giLayout,
                .causticLayout = causticLayout,
                .worldMaskLayout = currentWorldMaskLayout(),
                .colorAttachmentFormats = cachedColorFormats,
                .depthAttachmentFormat = cachedDepthFormat,
                .motionVectorsEnabled = mvEnabled
            };

            meshShaderPipeline->recreate(pipelineInfo);
            if (giLayout && giCascadeManager)
            {
                auto* storage = giCascadeManager->getProbeStorage();
                meshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
            }
            if (causticLayout)
                meshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());

            if (transparentMeshShaderPipeline)
            {
                pipelineInfo.transparentMode = true;
                transparentMeshShaderPipeline->recreate(pipelineInfo);
                pipelineInfo.transparentMode = false;
                if (giLayout && giCascadeManager)
                {
                    auto* storage = giCascadeManager->getProbeStorage();
                    transparentMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                }
                if (causticLayout)
                    transparentMeshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());
            }

            if (wboitMeshShaderPipeline && !cachedWBOITColorFormats.empty())
            {
                pipelineInfo.colorAttachmentFormats = cachedWBOITColorFormats;
                pipelineInfo.depthAttachmentFormat = cachedWBOITDepthFormat;
                pipelineInfo.wboitMode = true;
                wboitMeshShaderPipeline->recreate(pipelineInfo);
                if (giLayout && giCascadeManager)
                {
                    auto* storage = giCascadeManager->getProbeStorage();
                    wboitMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                }
                if (causticLayout)
                    wboitMeshShaderPipeline->updateCausticDescriptor(water.causticsResources->getDescriptorSet());
            }

            if (terrain.pipeline)
            {
                terrain.pipeline->recreate(cachedIBLLayout,
                                          bindlessTextures->getDescriptorSetLayout(),
                                          meshShaderPipeline->getMeshletDataLayout(),
                                          meshShaderPipeline->getVertexDataLayout(),
                                          lightBufferManager->getDescriptorSetLayout(),
                                          clusterGridManager->getDescriptorSetLayout(),
                                          lightCullingPipeline->getDescriptorSetLayout(),
                                          shadowSystem->getShadowDataLayout(),
                                          shadowSystem->getShadowTextureLayout(),
                                          cachedColorFormats, cachedDepthFormat);
            }

            if (water.pipeline)
            {
                vk::DescriptorSetLayout oceanLayout{};
                if (water.multiBandOceanLayout)
                    oceanLayout = water.multiBandOceanLayout;

                vk::DescriptorSetLayout refractionLayout{};
                if (water.refractionResources && water.refractionResources->isInitialized())
                    refractionLayout = water.refractionResources->getDescriptorSetLayout();

                water.pipeline->recreate({
                    cachedIBLLayout,
                    lightBufferManager->getDescriptorSetLayout(),
                    clusterGridManager->getDescriptorSetLayout(),
                    lightCullingPipeline->getDescriptorSetLayout(),
                    shadowSystem->getShadowDataLayout(),
                    shadowSystem->getShadowTextureLayout(),
                    oceanLayout,
                    refractionLayout,
                    cachedColorFormats, cachedDepthFormat
                });
            }

            if (vegetation.grassMeshPipeline && vegetation.grassMeshPipeline->isInitialized())
            {
                vegetation.grassMeshPipeline->recreate(
                    vegetation.windSystem ? vegetation.windSystem->getDescriptorSetLayout() : vk::DescriptorSetLayout{},
                    lightBufferManager ? lightBufferManager->getDescriptorSetLayout() : vk::DescriptorSetLayout{},
                    bindlessTextures ? bindlessTextures->getDescriptorSetLayout() : vk::DescriptorSetLayout{},
                    cachedColorFormats, cachedDepthFormat);
            }
        }
        else
        {
            vfLogError("GPUDrivenRenderer::recreatePipelines: Cannot recreate pipeline due to missing components");
        }
    }

}
