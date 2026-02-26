#include "GPUDrivenRenderer.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "resource/ResourceManager.hpp"
#include "../../core/SwapChain.hpp"
#include "print/Logger.hpp"
#include <chrono>

namespace render::gpudriven
{
    void GPUDrivenRenderer::initTerrainSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                                    vk::RenderPass renderPass)
    {
        terrainMeshBuffer = std::make_unique<TerrainMeshBuffer>(device);
        terrainMeshBuffer->init();

        terrainAdapter = std::make_unique<TerrainGPUAdapter>(*terrainMeshBuffer);
        terrainStreamManager = std::make_unique<TerrainStreamManager>(*terrainMeshBuffer, *terrainAdapter);

        if (pendingTileDataLoader_)
        {
            terrainStreamManager->setTileDataLoader(std::move(pendingTileDataLoader_));
            pendingTileDataLoader_ = nullptr;
        }
        if (pendingTileRAMEvictor_)
        {
            terrainStreamManager->setTileRAMEvictor(std::move(pendingTileRAMEvictor_));
            pendingTileRAMEvictor_ = nullptr;
        }

        terrainPipeline = std::make_unique<TerrainMeshShaderPipeline>(device, swapChain);
        terrainPipeline->init(
            iblDescriptorSetLayout,
            bindlessTextures->getDescriptorSetLayout(),
            meshShaderPipeline->getMeshletDataLayout(),
            meshShaderPipeline->getVertexDataLayout(),
            lightBufferManager->getDescriptorSetLayout(),
            clusterGridManager->getDescriptorSetLayout(),
            lightCullingPipeline->getDescriptorSetLayout(),
            shadowSystem->getShadowDataLayout(),
            shadowSystem->getShadowTextureLayout(),
            renderPass
        );
        loggerInfo("GPUDrivenRenderer: Terrain mesh shader pipeline initialized");

        shadowSystem->initTerrainShadowPass(
            terrainPipeline->getTerrainDataLayout(),
            terrainPipeline->getCachedMeshletLayout(),
            terrainPipeline->getCachedVertexLayout()
        );
        loggerInfo("GPUDrivenRenderer: Terrain shadow pass initialized");
    }

    void GPUDrivenRenderer::setTerrainFrustumCullingEnabled(bool enabled)
    {
        if (terrainPipeline)
        {
            terrainPipeline->setFrustumCullingEnabled(enabled);
        }
    }

    void GPUDrivenRenderer::setTerrainMeshletCullingEnabled(bool enabled)
    {
        if (terrainPipeline)
        {
            terrainPipeline->setMeshletCullingEnabled(enabled);
        }
    }

    void GPUDrivenRenderer::registerTerrainLayerTextures(const std::string& materialPath)
    {
        if (materialPath.empty() || materialPath == currentTerrainMaterialPath_)
        {
            return;
        }

        if (!bindlessTextures || !materialTextureCache)
        {
            return;
        }

        auto materialData = resource::ResourceManager::loadTerrainMaterial(materialPath);
        if (!materialData)
        {
            return;
        }

        terrainLayerData_.clear();
        terrainLayerData_.resize(materialData->activeLayerCount);

        for (uint8_t i = 0; i < materialData->activeLayerCount; ++i)
        {
            const auto& layer = materialData->layers[i];
            TerrainLayerGPUData& gpuLayer = terrainLayerData_[i];
            gpuLayer = {};

            auto tryRegisterLayerTex = [&](const std::string& texPath) -> uint32_t
            {
                if (texPath.empty()) return 0;
                if (!materialTextureCache->loadTexture(texPath)) return 0;
                vk::ImageView view = materialTextureCache->getViewForPath(texPath);
                vk::Sampler sampler = materialTextureCache->getSamplerForPath(texPath);
                if (!view || !sampler) return 0;
                return bindlessTextures->registerTexture(texPath, view, sampler);
            };

            gpuLayer.albedoTextureIndex = tryRegisterLayerTex(layer.albedoTexturePath);
            gpuLayer.normalTextureIndex = tryRegisterLayerTex(layer.normalTexturePath);

            gpuLayer.tilingScale = layer.tilingScale;
        }

        if (terrainPipeline)
        {
            terrainPipeline->updateTerrainLayerInfo(terrainLayerData_);
        }

        currentTerrainMaterialPath_ = materialPath;
        loggerInfo("GPUDrivenRenderer: Registered {} terrain layer textures from '{}'",
                   materialData->activeLayerCount, materialPath);
    }

    void GPUDrivenRenderer::updateTerrain(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                          const glm::vec3& cameraPosition,
                                          const std::string& terrainMaterialPath)
    {
        auto frameStart = std::chrono::high_resolution_clock::now();

        if (!initialized || !terrainRenderingEnabled || !terrainAdapter || !terrainPipeline)
        {
            return;
        }

        if (!terrainMaterialPath.empty())
        {
            registerTerrainLayerTextures(terrainMaterialPath);
        }

        if (visibleTiles.empty())
        {
            terrainTileData.clear();
            return;
        }

        auto streamStart = std::chrono::high_resolution_clock::now();

        if (terrainStreamManager)
        {
            terrainStreamManager->update(visibleTiles, cameraPosition);
        }
        else
        {
            for (terrain::TerrainTile* tile : visibleTiles)
            {
                if (!tile || !tile->isVisible)
                {
                    continue;
                }

                TerrainTileKey key{tile->coord.x, tile->coord.z};
                if (!terrainAdapter->hasTile(key))
                {
                    terrainAdapter->uploadTile(*tile);
                }
            }
        }

        auto streamEnd = std::chrono::high_resolution_clock::now();
        terrainStreamingUs_ = std::chrono::duration<float, std::micro>(streamEnd - streamStart).count();

        terrainAdapter->markGPUTileDataDirty();

        auto buildStart = std::chrono::high_resolution_clock::now();
        const auto& newTileData = terrainAdapter->buildGPUTileData(visibleTiles);
        auto buildEnd = std::chrono::high_resolution_clock::now();
        terrainBuildTileDataUs_ = std::chrono::duration<float, std::micro>(buildEnd - buildStart).count();

        auto uploadStart = std::chrono::high_resolution_clock::now();

        if (!newTileData.empty())
        {
            terrainTileData = newTileData;
            terrainPipeline->updateTileData(terrainTileData);
        }
        else
        {
            terrainTileData.clear();
        }

        auto uploadEnd = std::chrono::high_resolution_clock::now();
        terrainUploadTileDataUs_ = std::chrono::duration<float, std::micro>(uploadEnd - uploadStart).count();

        terrainUpdateUs_ = std::chrono::duration<float, std::micro>(uploadEnd - frameStart).count();
    }

    void GPUDrivenRenderer::clearTerrainData()
    {
        if (terrainStreamManager)
        {
            terrainStreamManager->clear();
        }
        else if (terrainAdapter)
        {
            terrainAdapter->clear();
        }
        terrainTileData.clear();
        currentTerrainMaterialPath_.clear();
        terrainLayerData_.clear();

        if (terrainPipeline)
        {
            terrainPipeline->updateTileData({});
            terrainPipeline->updateTerrainLayerInfo({});
        }

        if (terrainMeshBuffer)
        {
            terrainMeshBuffer->clear();
        }
    }

    void GPUDrivenRenderer::renderTerrainDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                              uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!initialized || !terrainRenderingEnabled || !terrainPipeline || !meshShaderPipeline)
        {
            return;
        }

        if (terrainTileData.empty())
        {
            return;
        }

        terrainPipeline->updateSharedDescriptors(
            iblDescriptorSet,
            bindlessTextures->getDescriptorSet(),
            lightBufferManager->getDescriptorSet(),
            clusterGridManager->getDescriptorSet(),
            lightCullingPipeline->getDescriptorSet(),
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowDataDescSet() : vk::DescriptorSet{},
            shadowSystem && shadowSystem->isInitialized() ? shadowSystem->getShadowTextureDescSet() : vk::DescriptorSet{}
        );

        // Use provided dimensions (RTT) or fall back to swapchain extent (main viewport)
        float dispatchWidth, dispatchHeight;
        if (screenWidth > 0 && screenHeight > 0)
        {
            dispatchWidth = static_cast<float>(screenWidth);
            dispatchHeight = static_cast<float>(screenHeight);
        }
        else
        {
            auto extent = swapChain.getSwapchainExtent();
            dispatchWidth = static_cast<float>(extent.width);
            dispatchHeight = static_cast<float>(extent.height);
        }

        float terrainDistSq = 0.0f;
        if (distanceCullingEnabled)
        {
            float d = categoryDistances[ObjectCategory::Terrain];
            terrainDistSq = d * d;
        }
        terrainPipeline->setTerrainMaxDrawDistSq(terrainDistSq);

        uint32_t viewMode = currentViewMode;
        if (meshletFrustumCullingEnabled) viewMode |= TERRAIN_CULL_FRUSTUM_BIT;
        if (meshletBackfaceCullingEnabled) viewMode |= TERRAIN_CULL_BACKFACE_BIT;

        terrainPipeline->dispatch(
            cmd,
            viewMode,
            dispatchWidth,
            dispatchHeight,
            terrainLODBias,
            terrainErrorThreshold,
            terrainTextureScale
        );
    }

    void GPUDrivenRenderer::setBrushOverlay(const glm::vec2& worldPos, float worldRadius, float falloff, float shape)
    {
        if (terrainPipeline)
        {
            terrainPipeline->setBrushOverlay(worldPos, worldRadius, falloff, shape);
        }
    }

    void GPUDrivenRenderer::setTileDataLoader(TerrainStreamManager::TileDataLoader loader)
    {
        if (terrainStreamManager)
        {
            terrainStreamManager->setTileDataLoader(std::move(loader));
        }
        else
        {
            pendingTileDataLoader_ = std::move(loader);
        }
    }

    void GPUDrivenRenderer::setTileRAMEvictor(TerrainStreamManager::TileRAMEvictor evictor)
    {
        if (terrainStreamManager)
        {
            terrainStreamManager->setTileRAMEvictor(std::move(evictor));
        }
        else
        {
            pendingTileRAMEvictor_ = std::move(evictor);
        }
    }

    const TerrainStreamingStats* GPUDrivenRenderer::getTerrainStreamingStats() const
    {
        if (terrainStreamManager)
        {
            return &terrainStreamManager->getStats();
        }
        return nullptr;
    }

    TerrainCullingStats GPUDrivenRenderer::getTerrainCullingStats()
    {
        if (terrainPipeline)
        {
            return terrainPipeline->readStats();
        }
        return {};
    }
}
