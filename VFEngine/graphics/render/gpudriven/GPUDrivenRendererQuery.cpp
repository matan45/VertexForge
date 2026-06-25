#include "GPUDrivenRenderer.hpp"
#include "../occlusion/HiZBuffer.hpp"
// VK-1443: full manager types (forward-declared in GPUDrivenRenderer.hpp) constructed /
// dereferenced in this TU's init + query paths.
#include "../occlusion/LightOcclusionCulling.hpp"
#include "../volumetric/FogVolumeBufferManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "print/Log.hpp"

namespace render::gpudriven
{
    void GPUDrivenRenderer::setDefaultTexture(vk::ImageView view, vk::Sampler sampler)
    {
        if (!initialized || !bindlessTextures)
        {
            return;
        }

        bindlessTextures->setDefaultTexture(view, sampler);
    }

    uint32_t GPUDrivenRenderer::getMergedVertexCount() const
    {
        return mergedBuffer ? mergedBuffer->getTotalVertexCount() : 0;
    }

    uint32_t GPUDrivenRenderer::getMergedIndexCount() const
    {
        return mergedBuffer ? mergedBuffer->getTotalIndexCount() : 0;
    }

    uint32_t GPUDrivenRenderer::getRegisteredMeshCount() const
    {
        return mergedBuffer ? static_cast<uint32_t>(mergedBuffer->getRegisteredMeshes().size()) : 0;
    }

    uint32_t GPUDrivenRenderer::getRegisteredTextureCount() const
    {
        return bindlessTextures ? bindlessTextures->getRegisteredTextureCount() : 0;
    }

    uint32_t GPUDrivenRenderer::getBatchCount() const
    {
        return batchManager ? batchManager->getBatchCount() : 0;
    }

    uint32_t GPUDrivenRenderer::getCommandsPerBatch() const
    {
        return batchManager ? batchManager->getCommandsPerBatch() : 0;
    }

    uint32_t GPUDrivenRenderer::getTotalCapacity() const
    {
        return batchManager ? batchManager->getTotalCapacity() : 0;
    }

    uint64_t GPUDrivenRenderer::getDrawCommandBufferSize() const
    {
        return batchManager ? batchManager->getCombinedDrawCommandBufferSize() : 0;
    }

    uint64_t GPUDrivenRenderer::getDrawCountBufferSize() const
    {
        return batchManager ? batchManager->getCombinedDrawCountBufferSize() : 0;
    }

    uint64_t GPUDrivenRenderer::getPerDrawDataBufferSize() const
    {
        return batchManager ? batchManager->getCombinedPerDrawDataBufferSize() : 0;
    }

    uint64_t GPUDrivenRenderer::getTotalMemoryUsage() const
    {
        if (!batchManager) return 0;
        return batchManager->getCombinedDrawCommandBufferSize() +
            batchManager->getCombinedDrawCountBufferSize() +
            batchManager->getCombinedPerDrawDataBufferSize();
    }

    void GPUDrivenRenderer::updateStatsFromGPU()
    {
        if (!initialized || !enabled || !batchManager)
        {
            return;
        }

        GPUDrivenStats aggregated = batchManager->readBackAggregatedStats();

        stats.visibleObjects = aggregated.visibleObjects;
        stats.drawCalls = aggregated.drawCalls;

        stats.objectsLOD0 = aggregated.objectsLOD0;
        stats.objectsLOD1 = aggregated.objectsLOD1;
        stats.objectsLOD2 = aggregated.objectsLOD2;
        stats.objectsLOD3 = aggregated.objectsLOD3;

        stats.culledByFrustum = aggregated.culledByFrustum;
        stats.culledByOcclusion = aggregated.culledByOcclusion;
        stats.culledByDistance = aggregated.culledByDistance;
    }

    MeshletCullingStats GPUDrivenRenderer::getMeshletCullingStats()
    {
        if (!meshShaderPipeline)
        {
            return MeshletCullingStats{};
        }
        return meshShaderPipeline->readStats();
    }

    void GPUDrivenRenderer::setVisibleLightsFromBVH(const std::vector<uint32_t>& visibleLights)
    {
        lightCulling.visibleLightIds.clear();
        lightCulling.visibleLightIds.insert(visibleLights.begin(), visibleLights.end());
        lightCulling.useBVH = true;
    }

    void GPUDrivenRenderer::clearVisibleLights()
    {
        lightCulling.visibleLightIds.clear();
        lightCulling.useBVH = false;
    }

    void GPUDrivenRenderer::setWireframeMode(bool enabled)
    {
        if (wireframeMode == enabled) return;
        wireframeMode = enabled;

        device.getLogicalDevice().waitIdle();

        vk::DescriptorSetLayout giLayout{};
        if (giCascadeManager && giCascadeManager->getProbeStorage())
            giLayout = giCascadeManager->getProbeStorage()->getSamplingLayout();

        vk::DescriptorSetLayout causticLayout{};
        if (water.causticsResources && water.causticsResources->isInitialized())
            causticLayout = water.causticsResources->getDescriptorSetLayout();

        // Recreate scene mesh pipelines
        if (meshShaderPipeline)
        {
            meshShaderPipeline->setWireframeMode(enabled);

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
                .depthAttachmentFormat = cachedDepthFormat
            };

            meshShaderPipeline->recreate(pipelineInfo);

            if (transparentMeshShaderPipeline)
            {
                transparentMeshShaderPipeline->setWireframeMode(enabled);
                pipelineInfo.transparentMode = true;
                transparentMeshShaderPipeline->recreate(pipelineInfo);
            }

            if (wboitMeshShaderPipeline && !cachedWBOITColorFormats.empty())
            {
                wboitMeshShaderPipeline->setWireframeMode(enabled);
                pipelineInfo.transparentMode = false;
                pipelineInfo.colorAttachmentFormats = cachedWBOITColorFormats;
                pipelineInfo.depthAttachmentFormat = cachedWBOITDepthFormat;
                pipelineInfo.wboitMode = true;
                wboitMeshShaderPipeline->recreate(pipelineInfo);
            }
        }

        // Recreate terrain pipeline
        if (terrain.pipeline)
        {
            terrain.pipeline->setWireframeMode(enabled);
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

        // Recreate water pipeline
        if (water.pipeline)
        {
            water.pipeline->setWireframeMode(enabled);

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
                cachedColorFormats,
                cachedDepthFormat
            });
        }
    }

    void GPUDrivenRenderer::setDeletionQueue(core::DeferredDeletionQueue* queue)
    {
        if (shadowSystem)
        {
            shadowSystem->setDeletionQueue(queue);
        }
        if (textureStreamManager)
        {
            textureStreamManager->setDeletionQueue(queue);
        }
    }

    void GPUDrivenRenderer::initLightOcclusionCulling(occlusion::HiZBuffer* hiZBuffer)
    {
        if (!hiZBuffer)
        {
            vfLogWarning("GPUDrivenRenderer: Cannot init light occlusion culling - HiZBuffer is null");
            return;
        }

        lightOcclusionCulling = std::make_unique<occlusion::LightOcclusionCulling>(device, swapChain);
        lightOcclusionCulling->init(hiZBuffer);
        lightCulling.useOcclusion = true;

    }

    void GPUDrivenRenderer::readBackLightOcclusionResults()
    {
        if (!lightCulling.useOcclusion || !lightOcclusionCulling || !lightOcclusionCulling->isInitialized())
        {
            return;
        }

        lightOcclusionCulling->markResultsReady();

        const auto& visibleLights = lightOcclusionCulling->getVisibleLightIds();

        lightCulling.prevFrameOccludedLights = lightOcclusionCulling->getOccludedLightIds();
        lightCulling.hasPrevFrameOcclusionData = true;

        lightCulling.lightsAfterHiZCull = static_cast<uint32_t>(visibleLights.size());
    }

    uint32_t GPUDrivenRenderer::getTotalSceneLights() const
    {
        return lightCulling.totalSceneLights;
    }

    uint32_t GPUDrivenRenderer::getLightsAfterBVHCull() const
    {
        return lightCulling.lightsAfterBVHCull;
    }

    uint32_t GPUDrivenRenderer::getLightsAfterHiZCull() const
    {
        return lightCulling.lightsAfterHiZCull;
    }

    void GPUDrivenRenderer::initVolumetricFog(::postprocess::VolumetricQuality quality)
    {
        if (!initialized || !clusterGridManager || !lightBufferManager || !lightCullingPipeline)
        {
            vfLogWarning("GPUDrivenRenderer: Cannot init volumetric fog - lighting subsystems not ready");
            return;
        }

        if (volumetricPipeline)
        {
            volumetricPipeline->cleanup();
            volumetricPipeline.reset();
        }

        auto volQuality = static_cast<volumetric::VolumetricQuality>(static_cast<uint8_t>(quality));

        if (!fogVolumeBufferManager)
        {
            fogVolumeBufferManager = std::make_unique<volumetric::FogVolumeBufferManager>(device);
            fogVolumeBufferManager->init();
        }

        volumetricPipeline = std::make_unique<volumetric::VolumetricPipeline>(device);
        volumetricPipeline->init(
            volQuality,
            clusterGridManager->getDescriptorSetLayout(),
            lightBufferManager->getDescriptorSetLayout(),
            lightCullingPipeline->getDescriptorSetLayout(),
            shadowSystem ? shadowSystem->getShadowDataLayout() : vk::DescriptorSetLayout{},
            shadowSystem ? shadowSystem->getShadowTextureLayout() : vk::DescriptorSetLayout{},
            fogVolumeBufferManager->getDescriptorSetLayout(),
            (giCascadeManager && giCascadeManager->getProbeStorage())
                ? giCascadeManager->getProbeStorage()->getComputeSamplingLayout()
                : vk::DescriptorSetLayout{});

    }

    void GPUDrivenRenderer::setVolumetricFogEnabled(bool value)
    {
        if (volumetricPipeline)
            volumetricPipeline->setEnabled(value);
    }

    bool GPUDrivenRenderer::isVolumetricFogEnabled() const
    {
        return volumetricPipeline && volumetricPipeline->isEnabled();
    }

    void GPUDrivenRenderer::updateVolumetricSettings(const ::postprocess::VolumetricFogSettings& settings)
    {
        cachedVolumetricSettings = settings;
    }
}
