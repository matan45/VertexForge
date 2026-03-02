#include "GPUDrivenRenderer.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"
#include "../../core/Texture.hpp"
#include "../../core/SwapChain.hpp"
#include "lightbake/LightmapAtlas.hpp"
#include "print/Logger.hpp"
#include <glm/gtc/packing.hpp>
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
            gpuLayer.ormTextureIndex = tryRegisterLayerTex(layer.ormTexturePath);

            gpuLayer.tilingScale = layer.tilingScale;
            gpuLayer.roughness = layer.roughness;
            gpuLayer.metallic = layer.metallic;
            gpuLayer.ao = layer.ao;
            gpuLayer.emissionStrength = layer.emissionStrength;
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

    void GPUDrivenRenderer::setTerrainLightmapData(const std::vector<TerrainTileLightmapData>& data)
    {
        if (!terrainAdapter)
        {
            return;
        }

        terrainAdapter->clearTileLightmapData();

        for (const auto& entry : data)
        {
            // Register lightmap texture in bindless if not already done
            if (!entry.lightmapPath.empty() &&
                registeredLightmapPaths.find(entry.lightmapPath) == registeredLightmapPaths.end())
            {
                // The lightmap texture should already be loaded by registerSceneLightmapTextures
                // for mesh objects. If not, we need to load it here too.
                if (lightmapTextureCache.find(entry.lightmapPath) == lightmapTextureCache.end())
                {
                    auto lmData = lightbake::LightmapAtlas::load(entry.lightmapPath);
                    if (lmData.width > 0 && lmData.height > 0 && !lmData.texels.empty())
                    {
                        resource::HDRData hdrData;
                        hdrData.width = lmData.width;
                        hdrData.height = lmData.height;
                        hdrData.numbersOfChannels = 4;
                        hdrData.pixels.resize(lmData.width * lmData.height * 4);

                        const uint32_t channels = lmData.channels;
                        for (uint32_t i = 0; i < lmData.width * lmData.height; ++i)
                        {
                            hdrData.pixels[i * 4 + 0] = (channels > 0) ? lmData.texels[i * channels + 0] : 0.0f;
                            hdrData.pixels[i * 4 + 1] = (channels > 1) ? lmData.texels[i * channels + 1] : 0.0f;
                            hdrData.pixels[i * 4 + 2] = (channels > 2) ? lmData.texels[i * channels + 2] : 0.0f;
                            hdrData.pixels[i * 4 + 3] = 1.0f;
                        }

                        auto texture = std::make_unique<core::Texture>(device);
                        texture->loadHDRFromData(hdrData, false);

                        vk::ImageView view = texture->getImageView();
                        vk::Sampler sampler = texture->getSampler();
                        if (view && sampler && bindlessTextures)
                        {
                            bindlessTextures->registerTexture(
                                entry.lightmapPath, view, sampler);
                            registeredLightmapPaths.insert(entry.lightmapPath);
                        }
                        lightmapTextureCache[entry.lightmapPath] = std::move(texture);
                    }
                }
            }

            // Resolve bindless texture index for this lightmap
            uint32_t textureIndex = INVALID_TEXTURE_INDEX;
            if (bindlessTextures && !entry.lightmapPath.empty())
            {
                textureIndex = bindlessTextures->getTextureIndex(entry.lightmapPath);
            }

            if (textureIndex != INVALID_TEXTURE_INDEX)
            {
                glm::uvec4 lmData(
                    textureIndex,
                    glm::packHalf2x16(glm::vec2(entry.scaleOffset.x, entry.scaleOffset.y)),
                    glm::packHalf2x16(glm::vec2(entry.scaleOffset.z, entry.scaleOffset.w)),
                    0
                );
                terrainAdapter->setTileLightmapData(entry.coordX, entry.coordZ, lmData);
            }
        }
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
