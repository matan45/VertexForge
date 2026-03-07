#include "GPUDrivenRenderer.hpp"
#include "water/WaterTile.hpp"
#include "water/WaterTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "print/Log.hpp"
#include <cstring>

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

        water.pipeline = std::make_unique<render::water::WaterPipeline>(device, swapChain);
        water.pipeline->init({
            iblDescriptorSetLayout,
            lightBufferManager->getDescriptorSetLayout(),
            clusterGridManager->getDescriptorSetLayout(),
            lightCullingPipeline->getDescriptorSetLayout(),
            shadowSystem->getShadowDataLayout(),
            shadowSystem->getShadowTextureLayout(),
            renderPass
        });

    }

    void GPUDrivenRenderer::updateWater(const std::vector<::water::WaterTile*>& visibleTiles,
                                          const ::water::WaterGlobalSettings& settings,
                                          const ::water::WaterTileConfig& tileConfig)
    {
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
    }

    void GPUDrivenRenderer::renderWaterDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
    {
        if (!initialized || !water.renderingEnabled || !water.pipeline || water.tileData.empty())
            return;

        render::water::WaterRenderDescriptors waterDescriptors{
            iblDescriptorSet,
            lightBufferManager->getDescriptorSet(),
            clusterGridManager->getDescriptorSet(),
            lightCullingPipeline->getDescriptorSet(),
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowDataDescSet() : vk::DescriptorSet{},
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowTextureDescSet() : vk::DescriptorSet{}
        };
        water.pipeline->render(cmd, waterDescriptors, *water.meshBuffer, water.cachedPushConstants);
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
}
