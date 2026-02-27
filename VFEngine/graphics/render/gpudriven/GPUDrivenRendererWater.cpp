#include "GPUDrivenRenderer.hpp"
#include "water/WaterTile.hpp"
#include "water/WaterTypes.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "print/Logger.hpp"

namespace render::gpudriven
{
    void GPUDrivenRenderer::initWaterSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                                  vk::RenderPass renderPass)
    {
        waterMeshBuffer = std::make_unique<render::water::WaterMeshBuffer>();
        waterMeshBuffer->init(
            device.getLogicalDevice(),
            device.getPhysicalDevice(),
            device.getGraphicsQueue(),
            device.getStagingCommandPool(),
            render::water::WATER_DEFAULT_SUBDIVISIONS
        );

        waterPipeline = std::make_unique<render::water::WaterPipeline>(device, swapChain);
        waterPipeline->init(iblDescriptorSetLayout, renderPass);

        loggerInfo("GPUDrivenRenderer: Water pipeline initialized");
    }

    void GPUDrivenRenderer::updateWater(const std::vector<::water::WaterTile*>& visibleTiles,
                                          const ::water::WaterGlobalSettings& settings,
                                          const ::water::WaterTileConfig& tileConfig)
    {
        if (!initialized || !waterRenderingEnabled || !waterPipeline)
            return;

        if (visibleTiles.empty())
        {
            waterTileData.clear();
            return;
        }

        waterTileData.resize(visibleTiles.size());
        for (size_t i = 0; i < visibleTiles.size(); ++i)
        {
            const auto* tile = visibleTiles[i];
            auto& gpuTile = waterTileData[i];
            gpuTile.worldOriginAndSize = glm::vec4(tile->worldOrigin, tileConfig.worldTileSize);
            gpuTile.heightAndWave = glm::vec4(tile->waterHeight, tile->waveIntensity, 0.0f, 0.0f);
        }

        waterMeshBuffer->updateTileData(waterTileData);

        waterPipeline->updateDescriptors(
            waterMeshBuffer->getTileSSBO(),
            static_cast<uint32_t>(waterTileData.size())
        );

        cachedWaterPushConstants.shallowColor = settings.shallowColor;
        cachedWaterPushConstants.deepColor = settings.deepColor;
        cachedWaterPushConstants.waveSpeed = settings.waveSpeed;
        cachedWaterPushConstants.waveAmplitude = settings.waveAmplitude;
        cachedWaterPushConstants.waveFrequency = settings.waveFrequency;
        cachedWaterPushConstants.maxVisibleDepth = settings.maxVisibleDepth;
        cachedWaterPushConstants.fresnelPower = settings.fresnelPower;
        cachedWaterPushConstants.dudvTiling = settings.dudvTiling;
        cachedWaterPushConstants.dudvStrength = settings.dudvStrength;
        cachedWaterPushConstants.waveDirection = glm::radians(settings.waveDirectionDegrees);
    }

    void GPUDrivenRenderer::renderWaterDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet)
    {
        if (!initialized || !waterRenderingEnabled || !waterPipeline || waterTileData.empty())
            return;

        waterPipeline->render(cmd, iblDescriptorSet, *waterMeshBuffer, cachedWaterPushConstants);
    }

    void GPUDrivenRenderer::clearWaterData()
    {
        waterTileData.clear();
    }
}
