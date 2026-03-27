#include "GPUDrivenRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "../water/OceanFFTResources.hpp"
#include "../../../services/data/OceanData.hpp"
#include "print/Log.hpp"
#include <cstring>
#include <chrono>

namespace render::gpudriven
{
    void GPUDrivenRenderer::initWaterSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                                  vk::RenderPass renderPass,
                                                  vk::ImageView sceneDepthView)
    {
        water.meshBuffer = std::make_unique<render::water::WaterMeshBuffer>();
        water.meshBuffer->init(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            render::water::WATER_DEFAULT_SUBDIVISIONS
        );

        // Create refraction resources before pipeline so we have the descriptor set layout
        water.refractionResources = std::make_unique<render::water::WaterRefractionResources>(device);
        water.refractionResources->init(
            swapChain.getSwapchainImageFormat(),
            swapChain.getSwapchainExtent().width,
            swapChain.getSwapchainExtent().height,
            sceneDepthView);

        // Ocean texture layout (from multi-band descriptor if initialized, otherwise WaterPipeline creates dummy)
        vk::DescriptorSetLayout oceanLayout{};
        if (water.multiBandOceanLayout)
            oceanLayout = water.multiBandOceanLayout;

        vk::DescriptorSetLayout refractionLayout = water.refractionResources->getDescriptorSetLayout();

        water.pipeline = std::make_unique<render::water::WaterPipeline>(device, swapChain);
        water.pipeline->init({
            iblDescriptorSetLayout,
            lightBufferManager->getDescriptorSetLayout(),
            clusterGridManager->getDescriptorSetLayout(),
            lightCullingPipeline->getDescriptorSetLayout(),
            shadowSystem->getShadowDataLayout(),
            shadowSystem->getShadowTextureLayout(),
            oceanLayout,
            refractionLayout,
            renderPass
        });

    }

    void GPUDrivenRenderer::updateWater(const services::OceanVisualSettings& visualSettings,
                                          float baseWaterHeight,
                                          const glm::vec3& cameraPosition,
                                          float oceanPatchSize)
    {
        auto updateStart = std::chrono::high_resolution_clock::now();
        if (!initialized || !water.renderingEnabled || !water.pipeline)
            return;

        // Generate single large ocean plane centered on camera,
        // snapped to patch-size grid so FFT textures don't shift with camera movement
        float planeSize = oceanPatchSize * 10.0f;
        float halfSize = planeSize * 0.5f;

        float snappedX = std::floor(cameraPosition.x / oceanPatchSize) * oceanPatchSize;
        float snappedZ = std::floor(cameraPosition.z / oceanPatchSize) * oceanPatchSize;

        water.tileData.resize(1);
        auto& gpuData = water.tileData[0];
        gpuData.worldOriginAndSize = glm::vec4(
            snappedX - halfSize,
            0.0f,
            snappedZ - halfSize,
            planeSize
        );
        gpuData.heightAndWave = glm::vec4(baseWaterHeight, 1.0f, 0.0f, 0.0f);

        water.meshBuffer->updateTileData(water.tileData);

        water.pipeline->updateDescriptors(
            water.meshBuffer->getTileSSBO(),
            static_cast<uint32_t>(water.tileData.size())
        );

        water.cachedPushConstants.shallowColor = visualSettings.shallowColor;
        water.cachedPushConstants.deepColor = visualSettings.deepColor;
        water.cachedPushConstants.maxVisibleDepth = visualSettings.maxVisibleDepth;
        water.cachedPushConstants.fresnelPower = visualSettings.fresnelPower;
        water.cachedPushConstants.refractionStrength = visualSettings.refractionStrength;
        water.cachedPushConstants.refractionChromatic = visualSettings.refractionChromatic;
        water.cachedPushConstants.refractionDepthScale = visualSettings.refractionDepthScale;

        // Update caustic params UBO
        if (water.causticsResources && water.causticsResources->isInitialized())
        {
            render::water::CausticParams params;
            params.waterHeight = baseWaterHeight;
            params.causticStrength = visualSettings.causticStrength;
            params.depthFalloff = visualSettings.causticDepthFalloff;
            params.patchSize = oceanPatchSize;
            water.causticsResources->updateParams(params);
        }

        auto updateEnd = std::chrono::high_resolution_clock::now();
        water.updateUs = std::chrono::duration<float, std::micro>(updateEnd - updateStart).count();
    }

    void GPUDrivenRenderer::renderWaterDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
    {
        auto renderStart = std::chrono::high_resolution_clock::now();
        if (!initialized || !water.renderingEnabled || !water.pipeline || water.tileData.empty())
            return;

        // Set per-band ocean push constant fields
        uint32_t bandMask = 0;
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (water.oceanBands[i] && water.oceanBands[i]->isInitialized())
                bandMask |= (1u << i);
        }
        water.cachedPushConstants.bandEnableMask = bandMask;

        if (water.oceanEnabled && water.oceanBands[0] && water.oceanBands[0]->isInitialized())
        {
            const auto& cfg0 = water.oceanBands[0]->getConfig();
            water.cachedPushConstants.oceanChoppiness = cfg0.choppiness;
            water.cachedPushConstants.oceanPatchSize0 = cfg0.patchSize;
            water.cachedPushConstants.oceanFoamThreshold = cfg0.foamThreshold;
        }
        else
        {
            water.cachedPushConstants.oceanChoppiness = 0.0f;
            water.cachedPushConstants.oceanPatchSize0 = 1.0f;
            water.cachedPushConstants.oceanFoamThreshold = 0.0f;
        }

        water.cachedPushConstants.oceanPatchSize1 = (water.oceanBands[1] && water.oceanBands[1]->isInitialized())
            ? water.oceanBands[1]->getConfig().patchSize : 1.0f;
        water.cachedPushConstants.oceanPatchSize2 = (water.oceanBands[2] && water.oceanBands[2]->isInitialized())
            ? water.oceanBands[2]->getConfig().patchSize : 1.0f;

        render::water::WaterRenderDescriptors waterDescriptors{
            iblDescriptorSet,
            lightBufferManager->getDescriptorSet(),
            clusterGridManager->getDescriptorSet(),
            lightCullingPipeline->getDescriptorSet(),
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowDataDescSet() : vk::DescriptorSet{},
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowTextureDescSet() : vk::DescriptorSet{},
            (water.oceanEnabled && water.multiBandOceanDescSet)
                ? water.multiBandOceanDescSet : vk::DescriptorSet{},
            (water.refractionResources && water.refractionResources->isInitialized())
                ? water.refractionResources->getDescriptorSet() : vk::DescriptorSet{}
        };
        water.pipeline->render(cmd, waterDescriptors, *water.meshBuffer, water.cachedPushConstants);

        auto renderEnd = std::chrono::high_resolution_clock::now();
        water.renderUs = std::chrono::duration<float, std::micro>(renderEnd - renderStart).count();
    }

    void GPUDrivenRenderer::clearWaterData()
    {
        water.tileData.clear();
    }

    void GPUDrivenRenderer::initOceanFFT(const std::array<render::water::OceanFFTConfig, 3>& bandConfigs,
                                          const std::array<bool, 3>& bandEnabled)
    {
        device.getLogicalDevice().waitIdle();

        water.activeBandCount = 0;
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (bandEnabled[i])
            {
                if (!water.oceanBands[i])
                    water.oceanBands[i] = std::make_unique<render::water::OceanFFT>(device);
                water.oceanBands[i]->init(bandConfigs[i]);
                water.activeBandCount++;
            }
            else
            {
                if (water.oceanBands[i])
                {
                    water.oceanBands[i]->cleanup();
                    water.oceanBands[i].reset();
                }
            }
        }
        water.oceanEnabled = water.activeBandCount > 0;

        // Create composite 6-binding descriptor set
        createMultiBandOceanDescriptor();

        // Create caustics from band 0 (swell)
        if (water.oceanBands[0] && water.oceanBands[0]->isInitialized())
        {
            water.causticsResources = std::make_unique<render::water::WaterCausticsResources>(device);
            water.causticsResources->init(water.oceanBands[0]->getCausticView());
        }

        // Recreate water pipeline with multi-band ocean texture layout
        if (water.pipeline)
        {
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
                water.multiBandOceanLayout,
                refractionLayout,
                cachedRenderPass
            });
        }

        // Recreate mesh shader pipelines with caustic layout
        if (water.causticsResources && water.causticsResources->isInitialized())
        {
            vk::DescriptorSetLayout causticLayout = water.causticsResources->getDescriptorSetLayout();
            vk::DescriptorSet causticDescSet = water.causticsResources->getDescriptorSet();

            if (meshShaderPipeline && shadowSystem)
            {
                vk::DescriptorSetLayout giLayout{};
                if (giCascadeManager)
                {
                    auto* storage = giCascadeManager->getProbeStorage();
                    if (storage && storage->isInitialized())
                        giLayout = storage->getSamplingLayout();
                }

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
                    .renderPass = cachedRenderPass
                };

                meshShaderPipeline->recreate(pipelineInfo);
                meshShaderPipeline->updateCausticDescriptor(causticDescSet);
                if (giLayout && giCascadeManager)
                {
                    auto* storage = giCascadeManager->getProbeStorage();
                    meshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                }

                if (transparentMeshShaderPipeline)
                {
                    pipelineInfo.transparentMode = true;
                    transparentMeshShaderPipeline->recreate(pipelineInfo);
                    transparentMeshShaderPipeline->updateCausticDescriptor(causticDescSet);
                    if (giLayout && giCascadeManager)
                    {
                        auto* storage = giCascadeManager->getProbeStorage();
                        transparentMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                    }
                    pipelineInfo.transparentMode = false;
                }

                if (wboitMeshShaderPipeline && cachedWBOITRenderPass)
                {
                    pipelineInfo.renderPass = cachedWBOITRenderPass;
                    pipelineInfo.wboitMode = true;
                    wboitMeshShaderPipeline->recreate(pipelineInfo);
                    wboitMeshShaderPipeline->updateCausticDescriptor(causticDescSet);
                    if (giLayout && giCascadeManager)
                    {
                        auto* storage = giCascadeManager->getProbeStorage();
                        wboitMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                    }
                }
            }

            // Recreate terrain pipeline with caustic layout
            if (terrain.pipeline)
            {
                terrain.pipeline->setCausticEnabled(true, causticLayout);
                terrain.pipeline->recreate(cachedIBLLayout,
                                           bindlessTextures->getDescriptorSetLayout(),
                                           meshShaderPipeline->getMeshletDataLayout(),
                                           meshShaderPipeline->getVertexDataLayout(),
                                           lightBufferManager->getDescriptorSetLayout(),
                                           clusterGridManager->getDescriptorSetLayout(),
                                           lightCullingPipeline->getDescriptorSetLayout(),
                                           shadowSystem->getShadowDataLayout(),
                                           shadowSystem->getShadowTextureLayout(),
                                           cachedRenderPass);
                terrain.pipeline->updateCausticDescriptor(causticDescSet);
            }
        }

        vfLogInfo("GPUDrivenRenderer: Ocean FFT initialized with {} active bands", water.activeBandCount);
    }

    void GPUDrivenRenderer::createMultiBandOceanDescriptor()
    {
        vk::Device vkDevice = device.getLogicalDevice();

        // Cleanup old
        if (water.multiBandOceanPool)
        {
            vkDevice.destroyDescriptorPool(water.multiBandOceanPool);
            water.multiBandOceanPool = nullptr;
        }
        if (water.multiBandOceanLayout)
        {
            vkDevice.destroyDescriptorSetLayout(water.multiBandOceanLayout);
            water.multiBandOceanLayout = nullptr;
        }
        water.multiBandOceanDescSet = nullptr;

        // Create layout: 6 combined image samplers (vertex + fragment)
        std::array<vk::DescriptorSetLayoutBinding, 6> bindings{};
        for (uint32_t i = 0; i < 6; ++i)
        {
            bindings[i].binding = i;
            bindings[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
        }

        vk::DescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.bindingCount = 6;
        layoutInfo.pBindings = bindings.data();
        water.multiBandOceanLayout = vkDevice.createDescriptorSetLayout(layoutInfo);

        // Create pool
        vk::DescriptorPoolSize poolSize{vk::DescriptorType::eCombinedImageSampler, 6};
        vk::DescriptorPoolCreateInfo poolInfo{};
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        water.multiBandOceanPool = vkDevice.createDescriptorPool(poolInfo);

        // Allocate set
        vk::DescriptorSetAllocateInfo allocInfo{};
        allocInfo.descriptorPool = water.multiBandOceanPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &water.multiBandOceanLayout;
        water.multiBandOceanDescSet = vkDevice.allocateDescriptorSets(allocInfo)[0];

        // Update with band textures
        updateMultiBandOceanDescriptor();
    }

    void GPUDrivenRenderer::updateMultiBandOceanDescriptor()
    {
        if (!water.multiBandOceanDescSet)
            return;

        // Find a valid band to use as fallback for disabled bands
        render::water::OceanFFTResources* fallbackResources = nullptr;
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (water.oceanBands[i] && water.oceanBands[i]->isInitialized())
            {
                fallbackResources = water.oceanBands[i]->getResources();
                break;
            }
        }

        if (!fallbackResources)
            return;

        std::array<vk::DescriptorImageInfo, 6> imageInfos{};

        for (uint32_t i = 0; i < 3; ++i)
        {
            render::water::OceanFFTResources* resources = fallbackResources;
            if (water.oceanBands[i] && water.oceanBands[i]->isInitialized())
                resources = water.oceanBands[i]->getResources();

            vk::Sampler sampler = resources->getOutputSampler();
            imageInfos[i * 2 + 0] = {sampler, resources->getDisplacementView(), vk::ImageLayout::eShaderReadOnlyOptimal};
            imageInfos[i * 2 + 1] = {sampler, resources->getNormalView(), vk::ImageLayout::eShaderReadOnlyOptimal};
        }

        // Write all 6 descriptors
        std::array<vk::WriteDescriptorSet, 6> writes{};
        for (uint32_t i = 0; i < 6; ++i)
        {
            writes[i].dstSet = water.multiBandOceanDescSet;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = vk::DescriptorType::eCombinedImageSampler;
            writes[i].pImageInfo = &imageInfos[i];
        }
        device.getLogicalDevice().updateDescriptorSets(writes, nullptr);
    }

    void GPUDrivenRenderer::cleanupOceanFFT()
    {
        water.oceanEnabled = false;

        // Clear caustic descriptor references from pipelines before destroying resources
        if (meshShaderPipeline) meshShaderPipeline->updateCausticDescriptor(vk::DescriptorSet{});
        if (transparentMeshShaderPipeline) transparentMeshShaderPipeline->updateCausticDescriptor(vk::DescriptorSet{});
        if (wboitMeshShaderPipeline) wboitMeshShaderPipeline->updateCausticDescriptor(vk::DescriptorSet{});
        if (terrain.pipeline)
        {
            terrain.pipeline->updateCausticDescriptor(vk::DescriptorSet{});
            terrain.pipeline->setCausticEnabled(false);
        }

        if (water.causticsResources)
        {
            water.causticsResources->cleanup();
            water.causticsResources.reset();
        }

        device.getLogicalDevice().waitIdle();

        // Cleanup all bands
        for (auto& band : water.oceanBands)
        {
            if (band)
            {
                band->cleanup();
                band.reset();
            }
        }
        water.activeBandCount = 0;

        // Cleanup composite descriptor
        vk::Device vkDevice = device.getLogicalDevice();
        if (water.multiBandOceanPool)
        {
            vkDevice.destroyDescriptorPool(water.multiBandOceanPool);
            water.multiBandOceanPool = nullptr;
        }
        if (water.multiBandOceanLayout)
        {
            vkDevice.destroyDescriptorSetLayout(water.multiBandOceanLayout);
            water.multiBandOceanLayout = nullptr;
        }
        water.multiBandOceanDescSet = nullptr;

        // Recreate mesh shader pipelines without caustic layout
        if (meshShaderPipeline && shadowSystem)
        {
            vk::DescriptorSetLayout giLayout{};
            if (giCascadeManager)
            {
                auto* storage = giCascadeManager->getProbeStorage();
                if (storage && storage->isInitialized())
                    giLayout = storage->getSamplingLayout();
            }

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
                .causticLayout = nullptr,
                .renderPass = cachedRenderPass
            };

            meshShaderPipeline->recreate(pipelineInfo);
            if (giLayout && giCascadeManager)
            {
                auto* storage = giCascadeManager->getProbeStorage();
                meshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
            }

            if (transparentMeshShaderPipeline)
            {
                pipelineInfo.transparentMode = true;
                transparentMeshShaderPipeline->recreate(pipelineInfo);
                if (giLayout && giCascadeManager)
                {
                    auto* storage = giCascadeManager->getProbeStorage();
                    transparentMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                }
                pipelineInfo.transparentMode = false;
            }

            if (wboitMeshShaderPipeline && cachedWBOITRenderPass)
            {
                pipelineInfo.renderPass = cachedWBOITRenderPass;
                pipelineInfo.wboitMode = true;
                wboitMeshShaderPipeline->recreate(pipelineInfo);
                if (giLayout && giCascadeManager)
                {
                    auto* storage = giCascadeManager->getProbeStorage();
                    wboitMeshShaderPipeline->updateGIProbeDescriptor(storage->getSamplingDescSet());
                }
            }
        }

        // Recreate terrain pipeline without caustic layout
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
                                       cachedRenderPass);
        }

        // Recreate water pipeline with dummy ocean layout
        if (water.pipeline)
        {
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
                vk::DescriptorSetLayout{},
                refractionLayout,
                cachedRenderPass
            });
        }
    }

    void GPUDrivenRenderer::setOceanEnabled(bool enabled)
    {
        water.oceanEnabled = enabled;
        if (enabled)
        {
            water.activeBandCount = 0;
            for (auto& band : water.oceanBands)
                if (band && band->isInitialized()) water.activeBandCount++;
            water.oceanEnabled = water.activeBandCount > 0;
        }
    }

    void GPUDrivenRenderer::updateOceanConfig(const std::array<render::water::OceanFFTConfig, 3>& bandConfigs,
                                               const std::array<bool, 3>& bandEnabled)
    {
        bool needsRecreate = false;
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (bandEnabled[i] && water.oceanBands[i] && water.oceanBands[i]->isInitialized())
                water.oceanBands[i]->updateConfig(bandConfigs[i]);

            // Check if a band was toggled
            bool wasActive = water.oceanBands[i] && water.oceanBands[i]->isInitialized();
            if (bandEnabled[i] != wasActive)
                needsRecreate = true;
        }

        if (needsRecreate)
            initOceanFFT(bandConfigs, bandEnabled);
    }

    void GPUDrivenRenderer::dispatchOceanFFT(vk::CommandBuffer cmd, float time)
    {
        if (!water.oceanEnabled)
            return;

        auto dispatchStart = std::chrono::high_resolution_clock::now();
        for (uint32_t i = 0; i < 3; ++i)
        {
            if (water.oceanBands[i] && water.oceanBands[i]->isInitialized())
            {
                water.oceanBands[i]->dispatch(cmd, time);
                water.oceanBands[i]->insertBarrier(cmd);
            }
        }
        auto dispatchEnd = std::chrono::high_resolution_clock::now();
        water.dispatchUs = std::chrono::duration<float, std::micro>(dispatchEnd - dispatchStart).count();
    }

    void GPUDrivenRenderer::readbackOceanDisplacement()
    {
        if (!water.oceanEnabled || !water.oceanBands[0] || !water.oceanBands[0]->isInitialized())
            return;

        // Only readback band 0 (swell) for physics
        auto readbackStart = std::chrono::high_resolution_clock::now();
        water.oceanBands[0]->readbackDisplacementData();
        auto readbackEnd = std::chrono::high_resolution_clock::now();
        water.readbackUs = std::chrono::duration<float, std::micro>(readbackEnd - readbackStart).count();
    }

    float GPUDrivenRenderer::getOceanHeightAt(const glm::vec2& worldXZ) const
    {
        if (!water.oceanEnabled || !water.oceanBands[0] || !water.oceanBands[0]->isInitialized())
            return 0.0f;

        return water.oceanBands[0]->sampleHeightAt(worldXZ);
    }

    void GPUDrivenRenderer::copySceneColorForRefraction(vk::CommandBuffer cmd, vk::Image colorImage,
                                                         uint32_t width, uint32_t height)
    {
        if (water.refractionResources && water.refractionResources->isInitialized())
            water.refractionResources->copySceneColor(cmd, colorImage, width, height);
    }

    void GPUDrivenRenderer::recreateRefractionResources(vk::ImageView sceneDepthView)
    {
        if (!water.refractionResources)
            return;

        water.refractionResources->recreate(
            swapChain.getSwapchainImageFormat(),
            swapChain.getSwapchainExtent().width,
            swapChain.getSwapchainExtent().height,
            sceneDepthView);
    }
}
