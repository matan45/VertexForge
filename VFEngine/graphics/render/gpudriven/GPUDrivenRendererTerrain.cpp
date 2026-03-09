#include "GPUDrivenRenderer.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"
#include "../../core/Texture.hpp"
#include "../../core/SwapChain.hpp"
#include "lightbake/LightmapAtlas.hpp"
#include "print/Log.hpp"
#include <glm/gtc/packing.hpp>
#include <chrono>

namespace render::gpudriven
{
    void GPUDrivenRenderer::initTerrainSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                                    vk::RenderPass renderPass)
    {
        terrain.meshBuffer = std::make_unique<TerrainMeshBuffer>(device);
        terrain.meshBuffer->init();

        terrain.adapter = std::make_unique<TerrainGPUAdapter>(*terrain.meshBuffer);
        terrain.streamManager = std::make_unique<TerrainStreamManager>(*terrain.meshBuffer, *terrain.adapter);

        if (terrain.pendingTileDataLoader)
        {
            terrain.streamManager->setTileDataLoader(std::move(terrain.pendingTileDataLoader));
            terrain.pendingTileDataLoader = nullptr;
        }
        if (terrain.pendingTileRAMEvictor)
        {
            terrain.streamManager->setTileRAMEvictor(std::move(terrain.pendingTileRAMEvictor));
            terrain.pendingTileRAMEvictor = nullptr;
        }

        terrain.pipeline = std::make_unique<TerrainMeshShaderPipeline>(device, swapChain);
        terrain.pipeline->init(
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

        shadowSystem->initTerrainShadowPass(
            terrain.pipeline->getTerrainDataLayout(),
            terrain.pipeline->getCachedMeshletLayout(),
            terrain.pipeline->getCachedVertexLayout()
        );
    }

    void GPUDrivenRenderer::setTerrainFrustumCullingEnabled(bool enabled)
    {
        if (terrain.pipeline)
        {
            terrain.pipeline->setFrustumCullingEnabled(enabled);
        }
    }

    void GPUDrivenRenderer::setTerrainMeshletCullingEnabled(bool enabled)
    {
        if (terrain.pipeline)
        {
            terrain.pipeline->setMeshletCullingEnabled(enabled);
        }
    }

    void GPUDrivenRenderer::registerTerrainLayerTextures(const std::string& materialPath)
    {
        if (materialPath.empty())
        {
            return;
        }

        if (materialPath == terrain.currentMaterialPath && !terrain.layerDataDirty)
        {
            return;
        }

        if (!bindlessTextures || !materials.textureCache)
        {
            return;
        }

        auto materialData = resource::ResourceManager::loadTerrainMaterial(materialPath);
        if (!materialData)
        {
            return;
        }

        terrain.layerData.clear();
        terrain.layerData.resize(materialData->activeLayerCount);

        for (uint8_t i = 0; i < materialData->activeLayerCount; ++i)
        {
            const auto& layer = materialData->layers[i];
            TerrainLayerGPUData& gpuLayer = terrain.layerData[i];
            gpuLayer = {};

            auto tryRegisterLayerTex = [&](const std::string& texPath) -> uint32_t
            {
                if (texPath.empty()) return 0;
                if (!materials.textureCache->loadTexture(texPath)) return 0;
                vk::ImageView view = materials.textureCache->getViewForPath(texPath);
                vk::Sampler sampler = materials.textureCache->getSamplerForPath(texPath);
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

        if (terrain.pipeline)
        {
            terrain.pipeline->updateTerrainLayerInfo(terrain.layerData);
        }

        terrain.currentMaterialPath = materialPath;
        terrain.layerDataDirty = false;
        vfLogInfo("GPUDrivenRenderer: Registered {} terrain layer textures from '{}'",
                   materialData->activeLayerCount, materialPath);
    }

    void GPUDrivenRenderer::updateTerrain(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                          const glm::vec3& cameraPosition,
                                          const std::string& terrainMaterialPath)
    {
        auto frameStart = std::chrono::high_resolution_clock::now();

        if (!initialized || !terrain.renderingEnabled || !terrain.adapter || !terrain.pipeline)
        {
            return;
        }

        if (!terrainMaterialPath.empty())
        {
            registerTerrainLayerTextures(terrainMaterialPath);
        }
        else if (terrain.layerData.empty())
        {
            TerrainLayerGPUData defaultLayer{};
            defaultLayer.albedoTextureIndex = 0;
            defaultLayer.normalTextureIndex = 0;
            defaultLayer.ormTextureIndex = 0;
            defaultLayer.tilingScale = 1.0f;
            defaultLayer.roughness = 0.8f;
            defaultLayer.metallic = 0.0f;
            defaultLayer.ao = 1.0f;
            defaultLayer.emissionStrength = 0.0f;
            terrain.layerData.push_back(defaultLayer);

            if (terrain.pipeline)
            {
                terrain.pipeline->updateTerrainLayerInfo(terrain.layerData);
            }
        }

        if (visibleTiles.empty())
        {
            terrain.tileData.clear();
            return;
        }

        auto streamStart = std::chrono::high_resolution_clock::now();

        if (terrain.streamManager)
        {
            terrain.streamManager->update(visibleTiles, cameraPosition);
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
                if (!terrain.adapter->hasTile(key))
                {
                    terrain.adapter->uploadTile(*tile);
                }
            }
        }

        auto streamEnd = std::chrono::high_resolution_clock::now();
        terrain.streamingUs = std::chrono::duration<float, std::micro>(streamEnd - streamStart).count();

        terrain.adapter->markGPUTileDataDirty();

        auto buildStart = std::chrono::high_resolution_clock::now();
        const auto& newTileData = terrain.adapter->buildGPUTileData(visibleTiles);
        auto buildEnd = std::chrono::high_resolution_clock::now();
        terrain.buildTileDataUs = std::chrono::duration<float, std::micro>(buildEnd - buildStart).count();

        auto uploadStart = std::chrono::high_resolution_clock::now();

        if (!newTileData.empty())
        {
            terrain.tileData = newTileData;
            terrain.pipeline->updateTileData(terrain.tileData);
        }
        else
        {
            terrain.tileData.clear();
        }

        auto uploadEnd = std::chrono::high_resolution_clock::now();
        terrain.uploadTileDataUs = std::chrono::duration<float, std::micro>(uploadEnd - uploadStart).count();

        terrain.updateUs = std::chrono::duration<float, std::micro>(uploadEnd - frameStart).count();
    }

    void GPUDrivenRenderer::setTerrainLightmapData(const std::vector<TerrainTileLightmapData>& data)
    {
        if (!terrain.adapter)
        {
            return;
        }

        terrain.adapter->clearTileLightmapData();

        for (const auto& entry : data)
        {
            // Register lightmap texture in bindless if not already done
            if (!entry.lightmapPath.empty() &&
                materials.registeredLightmapPaths.find(entry.lightmapPath) == materials.registeredLightmapPaths.end())
            {
                // The lightmap texture should already be loaded by registerSceneLightmapTextures
                // for mesh objects. If not, we need to load it here too.
                if (materials.lightmapTextureCache.find(entry.lightmapPath) == materials.lightmapTextureCache.end())
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
                            materials.registeredLightmapPaths.insert(entry.lightmapPath);
                        }
                        materials.lightmapTextureCache[entry.lightmapPath] = std::move(texture);
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
                terrain.adapter->setTileLightmapData(entry.coordX, entry.coordZ, lmData);
            }
        }
    }

    void GPUDrivenRenderer::clearTerrainData()
    {
        if (terrain.streamManager)
        {
            terrain.streamManager->clear();
        }
        else if (terrain.adapter)
        {
            terrain.adapter->clear();
        }
        terrain.tileData.clear();
        terrain.currentMaterialPath.clear();
        terrain.layerData.clear();

        if (terrain.pipeline)
        {
            terrain.pipeline->updateTileData({});
            terrain.pipeline->updateTerrainLayerInfo({});
        }

        if (terrain.meshBuffer)
        {
            terrain.meshBuffer->clear();
        }

        clearVegetationData();
    }

    void GPUDrivenRenderer::evictTerrainTile(int32_t coordX, int32_t coordZ)
    {
        if (terrain.streamManager)
        {
            terrain.streamManager->evictTile(coordX, coordZ);
        }
        else if (terrain.adapter)
        {
            render::gpudriven::TerrainTileKey key{coordX, coordZ};
            for (uint32_t lod = 0; lod < 4; ++lod)
            {
                terrain.adapter->removeTileLOD(key, lod);
            }
        }
        if (terrain.adapter)
            terrain.adapter->markGPUTileDataDirty();
    }

    void GPUDrivenRenderer::setSelectedTerrainTile(int32_t coordX, int32_t coordZ)
    {
        if (terrain.adapter)
            terrain.adapter->setSelectedTile(coordX, coordZ);
    }

    void GPUDrivenRenderer::clearSelectedTerrainTile()
    {
        if (terrain.adapter)
            terrain.adapter->clearSelectedTile();
    }

    void GPUDrivenRenderer::renderTerrainDraw(vk::CommandBuffer cmd, vk::DescriptorSet iblDescriptorSet,
                                              uint32_t screenWidth, uint32_t screenHeight)
    {
        if (!initialized || !terrain.renderingEnabled || !terrain.pipeline || !meshShaderPipeline)
        {
            return;
        }

        if (terrain.tileData.empty())
        {
            return;
        }

        terrain.pipeline->updateSharedDescriptors(
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
        if (culling.distanceCullingEnabled)
        {
            float d = culling.categoryDistances[ObjectCategory::Terrain];
            terrainDistSq = d * d;
        }
        terrain.pipeline->setTerrainMaxDrawDistSq(terrainDistSq);
        terrain.pipeline->setShadowLOD(terrain.shadowLOD);

        uint32_t viewMode = culling.currentViewMode;
        if (culling.meshletFrustumCullingEnabled) viewMode |= TERRAIN_CULL_FRUSTUM_BIT;
        if (culling.meshletBackfaceCullingEnabled) viewMode |= TERRAIN_CULL_BACKFACE_BIT;

        terrain.pipeline->dispatch(
            cmd,
            viewMode,
            dispatchWidth,
            dispatchHeight,
            terrain.lodBias,
            terrain.errorThreshold,
            terrain.textureScale
        );
    }

    void GPUDrivenRenderer::setBrushOverlay(const glm::vec2& worldPos, float worldRadius, float falloff, float shape)
    {
        if (terrain.pipeline)
        {
            terrain.pipeline->setBrushOverlay(worldPos, worldRadius, falloff, shape);
        }
    }

    void GPUDrivenRenderer::setTileDataLoader(TerrainStreamManager::TileDataLoader loader)
    {
        if (terrain.streamManager)
        {
            terrain.streamManager->setTileDataLoader(std::move(loader));
        }
        else
        {
            terrain.pendingTileDataLoader = std::move(loader);
        }
    }

    void GPUDrivenRenderer::setTileRAMEvictor(TerrainStreamManager::TileRAMEvictor evictor)
    {
        if (terrain.streamManager)
        {
            terrain.streamManager->setTileRAMEvictor(std::move(evictor));
        }
        else
        {
            terrain.pendingTileRAMEvictor = std::move(evictor);
        }
    }

    const TerrainStreamingStats* GPUDrivenRenderer::getTerrainStreamingStats() const
    {
        if (terrain.streamManager)
        {
            return &terrain.streamManager->getStats();
        }
        return nullptr;
    }

    TerrainCullingStats GPUDrivenRenderer::getTerrainCullingStats()
    {
        if (terrain.pipeline)
        {
            return terrain.pipeline->readStats();
        }
        return {};
    }
}
