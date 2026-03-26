#include "GPUDrivenRenderer.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
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

        // Ocean texture layout (from OceanFFT if initialized, otherwise WaterPipeline creates dummy)
        vk::DescriptorSetLayout oceanLayout{};
        if (water.oceanFFT && water.oceanFFT->isInitialized())
            oceanLayout = water.oceanFFT->getOceanTextureLayout();

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

        auto updateEnd = std::chrono::high_resolution_clock::now();
        water.updateUs = std::chrono::duration<float, std::micro>(updateEnd - updateStart).count();
    }

    void GPUDrivenRenderer::renderWaterDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
    {
        auto renderStart = std::chrono::high_resolution_clock::now();
        if (!initialized || !water.renderingEnabled || !water.pipeline || water.tileData.empty())
            return;

        // Set ocean push constant fields
        if (water.oceanEnabled && water.oceanFFT && water.oceanFFT->isInitialized())
        {
            const auto& cfg = water.oceanFFT->getConfig();
            water.cachedPushConstants.oceanChoppiness = cfg.choppiness;
            water.cachedPushConstants.oceanPatchSize = cfg.patchSize;
            water.cachedPushConstants.oceanFoamThreshold = cfg.foamThreshold;
        }
        else
        {
            water.cachedPushConstants.oceanChoppiness = 0.0f;
            water.cachedPushConstants.oceanPatchSize = 1.0f;
            water.cachedPushConstants.oceanFoamThreshold = 0.0f;
        }

        render::water::WaterRenderDescriptors waterDescriptors{
            iblDescriptorSet,
            lightBufferManager->getDescriptorSet(),
            clusterGridManager->getDescriptorSet(),
            lightCullingPipeline->getDescriptorSet(),
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowDataDescSet() : vk::DescriptorSet{},
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowTextureDescSet() : vk::DescriptorSet{},
            (water.oceanEnabled && water.oceanFFT && water.oceanFFT->isInitialized())
                ? water.oceanFFT->getOceanTextureDescSet() : vk::DescriptorSet{},
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

    void GPUDrivenRenderer::initOceanFFT(const render::water::OceanFFTConfig& config)
    {
        if (!water.oceanFFT)
            water.oceanFFT = std::make_unique<render::water::OceanFFT>(device);

        water.oceanFFT->init(config);
        water.oceanEnabled = true;

        // Recreate water pipeline so it uses the real ocean descriptor set layout
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
                water.oceanFFT->getOceanTextureLayout(),
                refractionLayout,
                cachedRenderPass
            });
        }

        vfLogInfo("GPUDrivenRenderer: Ocean FFT initialized");
    }

    void GPUDrivenRenderer::cleanupOceanFFT()
    {
        water.oceanEnabled = false;
        if (water.oceanFFT)
        {
            water.oceanFFT->cleanup();
            water.oceanFFT.reset();
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
        water.oceanEnabled = enabled && water.oceanFFT && water.oceanFFT->isInitialized();
    }

    void GPUDrivenRenderer::updateOceanConfig(const render::water::OceanFFTConfig& config)
    {
        if (water.oceanFFT && water.oceanFFT->isInitialized())
            water.oceanFFT->updateConfig(config);
    }

    void GPUDrivenRenderer::dispatchOceanFFT(vk::CommandBuffer cmd, float time)
    {
        if (!water.oceanEnabled || !water.oceanFFT || !water.oceanFFT->isInitialized())
        {
            return;
        }

        auto dispatchStart = std::chrono::high_resolution_clock::now();
        water.oceanFFT->dispatch(cmd, time);
        water.oceanFFT->insertBarrier(cmd);
        auto dispatchEnd = std::chrono::high_resolution_clock::now();
        water.dispatchUs = std::chrono::duration<float, std::micro>(dispatchEnd - dispatchStart).count();
    }

    void GPUDrivenRenderer::readbackOceanDisplacement()
    {
        if (!water.oceanEnabled || !water.oceanFFT || !water.oceanFFT->isInitialized())
            return;

        auto readbackStart = std::chrono::high_resolution_clock::now();
        water.oceanFFT->readbackDisplacementData();
        auto readbackEnd = std::chrono::high_resolution_clock::now();
        water.readbackUs = std::chrono::duration<float, std::micro>(readbackEnd - readbackStart).count();
    }

    float GPUDrivenRenderer::getOceanHeightAt(const glm::vec2& worldXZ) const
    {
        if (!water.oceanEnabled || !water.oceanFFT || !water.oceanFFT->isInitialized())
            return 0.0f;

        return water.oceanFFT->sampleHeightAt(worldXZ);
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
