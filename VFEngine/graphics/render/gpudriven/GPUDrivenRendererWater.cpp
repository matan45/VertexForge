#include "GPUDrivenRenderer.hpp"
#include "water/WaterTile.hpp"
#include "water/WaterTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "print/Log.hpp"
#include <cstring>
#include <chrono>

namespace render::gpudriven
{
    void GPUDrivenRenderer::initWaterSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                                  vk::RenderPass renderPass)
    {
        water.meshBuffer = std::make_unique<render::water::WaterMeshBuffer>();
        water.meshBuffer->init(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            render::water::WATER_DEFAULT_SUBDIVISIONS
        );

        // Ocean texture layout (from OceanFFT if initialized, otherwise WaterPipeline creates dummy)
        vk::DescriptorSetLayout oceanLayout{};
        if (water.oceanFFT && water.oceanFFT->isInitialized())
            oceanLayout = water.oceanFFT->getOceanTextureLayout();

        water.pipeline = std::make_unique<render::water::WaterPipeline>(device, swapChain);
        water.pipeline->init({
            iblDescriptorSetLayout,
            lightBufferManager->getDescriptorSetLayout(),
            clusterGridManager->getDescriptorSetLayout(),
            lightCullingPipeline->getDescriptorSetLayout(),
            shadowSystem->getShadowDataLayout(),
            shadowSystem->getShadowTextureLayout(),
            oceanLayout,
            renderPass
        });

    }

    void GPUDrivenRenderer::updateWater(const std::vector<::water::WaterTile*>& visibleTiles,
                                          const ::water::WaterGlobalSettings& settings,
                                          const ::water::WaterTileConfig& tileConfig)
    {
        auto updateStart = std::chrono::high_resolution_clock::now();
        if (!initialized || !water.renderingEnabled || !water.pipeline)
            return;

        if (visibleTiles.empty())
        {
            water.tileData.clear();
            return;
        }

        water.tileData.resize(visibleTiles.size());
        for (size_t i = 0; i < visibleTiles.size(); ++i)
        {
            const auto* tile = visibleTiles[i];
            auto& gpuTile = water.tileData[i];
            gpuTile.worldOriginAndSize = glm::vec4(tile->worldOrigin, tileConfig.worldTileSize);
            uint32_t flags = render::water::WaterTileFlags::None;
            if (water.hasSelectedTile && tile->coord.x == water.selectedCoordX && tile->coord.z == water.selectedCoordZ)
                flags |= render::water::WaterTileFlags::Selected;
            float flagsAsFloat;
            std::memcpy(&flagsAsFloat, &flags, sizeof(float));
            gpuTile.heightAndWave = glm::vec4(tile->waterHeight, tile->waveIntensity, flagsAsFloat, 0.0f);
        }

        water.meshBuffer->updateTileData(water.tileData);

        water.pipeline->updateDescriptors(
            water.meshBuffer->getTileSSBO(),
            static_cast<uint32_t>(water.tileData.size())
        );

        water.cachedPushConstants.shallowColor = settings.shallowColor;
        water.cachedPushConstants.deepColor = settings.deepColor;
        water.cachedPushConstants.waveSpeed = settings.waveSpeed;
        water.cachedPushConstants.waveAmplitude = settings.waveAmplitude;
        water.cachedPushConstants.waveFrequency = settings.waveFrequency;
        water.cachedPushConstants.maxVisibleDepth = settings.maxVisibleDepth;
        water.cachedPushConstants.fresnelPower = settings.fresnelPower;
        water.cachedPushConstants.dudvTiling = settings.dudvTiling;
        water.cachedPushConstants.dudvStrength = settings.dudvStrength;
        water.cachedPushConstants.waveDirection = glm::radians(settings.waveDirectionDegrees);

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
            water.cachedPushConstants.oceanEnabled = 1;
            water.cachedPushConstants.oceanChoppiness = cfg.choppiness;
            water.cachedPushConstants.oceanPatchSize = cfg.patchSize;
            water.cachedPushConstants.oceanFoamThreshold = cfg.foamThreshold;
        }
        else
        {
            water.cachedPushConstants.oceanEnabled = 0;
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
                ? water.oceanFFT->getOceanTextureDescSet() : vk::DescriptorSet{}
        };
        water.pipeline->render(cmd, waterDescriptors, *water.meshBuffer, water.cachedPushConstants);

        auto renderEnd = std::chrono::high_resolution_clock::now();
        water.renderUs = std::chrono::duration<float, std::micro>(renderEnd - renderStart).count();
    }

    void GPUDrivenRenderer::clearWaterData()
    {
        water.tileData.clear();
    }

    void GPUDrivenRenderer::setSelectedWaterTile(int32_t coordX, int32_t coordZ)
    {
        water.selectedCoordX = coordX;
        water.selectedCoordZ = coordZ;
        water.hasSelectedTile = true;
    }

    void GPUDrivenRenderer::clearSelectedWaterTile()
    {
        water.hasSelectedTile = false;
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
            water.pipeline->recreate({
                cachedIBLLayout,
                lightBufferManager->getDescriptorSetLayout(),
                clusterGridManager->getDescriptorSetLayout(),
                lightCullingPipeline->getDescriptorSetLayout(),
                shadowSystem->getShadowDataLayout(),
                shadowSystem->getShadowTextureLayout(),
                water.oceanFFT->getOceanTextureLayout(),
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
            water.pipeline->recreate({
                cachedIBLLayout,
                lightBufferManager->getDescriptorSetLayout(),
                clusterGridManager->getDescriptorSetLayout(),
                lightCullingPipeline->getDescriptorSetLayout(),
                shadowSystem->getShadowDataLayout(),
                shadowSystem->getShadowTextureLayout(),
                vk::DescriptorSetLayout{},
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
}
