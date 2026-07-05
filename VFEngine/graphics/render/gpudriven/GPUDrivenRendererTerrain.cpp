#include "GPUDrivenRenderer.hpp"
#include "terrain/TerrainRVTManager.hpp"
#include "terrain/TerrainRVTBaker.hpp"
#include "terrain/TerrainTile.hpp"
#include "terrain/TerrainMaterialTypes.hpp"
#include "../material/MaterialTextureCache.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/AssetLifecycleManager.hpp"
#include "resource/Types.hpp"
#include "asset/AssetRef.hpp"
#include "../../core/Texture.hpp"
#include "../../core/SwapChain.hpp"
#include "types/RenderSettings.hpp"
#include "print/Log.hpp"
#include <chrono>
#include <limits>

namespace render::gpudriven
{
    void GPUDrivenRenderer::initTerrainSubsystems(vk::DescriptorSetLayout iblDescriptorSetLayout,
                                                    const std::vector<vk::Format>& colorFormats, vk::Format depthFormat)
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
        if (terrain.pendingTileLoadContextProvider)
        {
            terrain.streamManager->setTileLoadContextProvider(std::move(terrain.pendingTileLoadContextProvider));
            terrain.pendingTileLoadContextProvider = nullptr;
        }

        terrain.pipeline = std::make_unique<TerrainMeshShaderPipeline>(device, swapChain);
        // VK-1209: compile the terrain pipeline RVT-ready (set 5 + RVT_ENABLED) when config enabled it
        // before terrain init. Runtime toggling requires a pipeline recreate (restart-scoped, per config).
        if (vtCache.rvtEnabled)
            terrain.pipeline->setRVTSampleEnabled(true);
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
            colorFormats, depthFormat
        );

        shadowSystem->initTerrainShadowPass(
            terrain.pipeline->getTerrainDataLayout(),
            terrain.pipeline->getCachedMeshletLayout(),
            terrain.pipeline->getCachedVertexLayout()
        );

        // VK-1209: the RVT bake pipeline (self-contained; binds the terrain pipeline's own sets).
        if (vtCache.rvtEnabled)
        {
            terrainRVTBaker = std::make_unique<TerrainRVTBaker>(device);
            terrainRVTBaker->init(terrain.pipeline->getWeightMapLayout(),
                                  bindlessTextures->getDescriptorSetLayout(),
                                  terrain.pipeline->getTerrainDataLayout(),
                                  {vk::Format::eR8G8B8A8Srgb, vk::Format::eR8G8B8A8Unorm});
        }
    }

    void GPUDrivenRenderer::applyVirtualTextureSettings(const types::VirtualTextureSettings& settings)
    {
        const bool rvtToggled = vtCache.rvtEnabled != settings.rvtEnabled;
        const bool svtToggled = vtCache.svtEnabled != settings.svtEnabled;

        vtCache.rvtEnabled = settings.rvtEnabled;
        vtCache.svtEnabled = settings.svtEnabled;
        vtCache.rvtPoolBudgetMB = settings.rvtPoolBudgetMB;
        vtCache.svtPoolBudgetMB = settings.svtPoolBudgetMB;
        vtCache.rvtTexelsPerMeter = settings.rvtTexelsPerMeter;
        vtCache.pagesPerFrame = settings.pagesPerFrame;
        vtCache.evictionAgeFrames = settings.evictionAgeFrames;
        vtCache.svtPageLinearMaps = settings.svtPageLinearMaps; // VK-1480: 2nd (Unorm) SVT pool (restart)

        // Runtime RVT toggle: rebuild the terrain pipeline so set 5 + RVT_ENABLED match the new
        // state, and create/tear down the RVT subsystems. The manager itself comes up on the next
        // updateTerrain (once world bounds are known) — which runs before the terrain draw — so the
        // set-5 descriptor is written before it is sampled. Pool byte budgets remain restart-scoped.
        if (rvtToggled && initialized && terrain.pipeline)
        {
            terrain.pipeline->setRVTSampleEnabled(vtCache.rvtEnabled);
            recreateTerrainPipelineForRVT();

            if (vtCache.rvtEnabled)
            {
                if (!terrainRVTBaker)
                {
                    terrainRVTBaker = std::make_unique<TerrainRVTBaker>(device);
                    terrainRVTBaker->init(terrain.pipeline->getWeightMapLayout(),
                                          bindlessTextures->getDescriptorSetLayout(),
                                          terrain.pipeline->getTerrainDataLayout(),
                                          {vk::Format::eR8G8B8A8Srgb, vk::Format::eR8G8B8A8Unorm});
                }
            }
            else
            {
                terrainRVT.reset();
                terrainRVTBaker.reset();
            }
        }

        // Runtime SVT toggle: rebuild the scene mesh pipelines (set-1 SVT bindings + SVT_ENABLED)
        // and create/tear down the SVT manager. Registration opt-in happens on the next material load.
        if (svtToggled && initialized)
            applySVTToggle();

        if (rvtToggled || svtToggled)
            vfLogInfo("VK-1209 virtual texturing: RVT={} SVT={} (rvtPool={}MB svtPool={}MB, {} pages/frame)",
                      vtCache.rvtEnabled, vtCache.svtEnabled,
                      vtCache.rvtPoolBudgetMB, vtCache.svtPoolBudgetMB, vtCache.pagesPerFrame);
    }

    void GPUDrivenRenderer::recreateTerrainPipelineForRVT()
    {
        if (!terrain.pipeline || !bindlessTextures || !meshShaderPipeline || !lightBufferManager ||
            !clusterGridManager || !lightCullingPipeline || !shadowSystem)
            return;

        // Same layouts + formats initTerrainSubsystems passed to init(); recreate() waits idle.
        terrain.pipeline->recreate(
            cachedIBLLayout,
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

    bool GPUDrivenRenderer::isTerrainRVTActive() const
    {
        return terrainRVT != nullptr && terrainRVT->isInitialized();
    }

    void GPUDrivenRenderer::updateTerrainRVTResidency()
    {
        if (!terrainRVT)
            return;
        terrainRVT->markFeedbackReady();   // the prior frame's copy has completed (fence-gated caller)
        terrainRVT->beginFrameReadback();  // decode requested pages
        terrainRVT->updateResidency(rvtFrameCounter++);
    }

    void GPUDrivenRenderer::bakeTerrainRVT(vk::CommandBuffer cmd)
    {
        if (!terrainRVT || !terrainRVTBaker || !terrainRVTBaker->isReady())
            return;

        terrainRVT->clearFeedback(cmd); // fresh feedback for this frame's terrain draw
        terrainRVT->uploadPageTable(cmd);

        const float texScale = terrain.textureScale > 0.0f ? terrain.textureScale : 0.1f;
        const std::vector<TerrainTileGPUData>& tiles = terrain.tileData;
        TerrainRVTBaker* baker = terrainRVTBaker.get();
        const vt::VTPhysicalPool* pool = terrainRVT->getPool();

        // recordBakes transitions the pool ShaderRead<->ColorAttachment around this callback.
        terrainRVT->recordBakes(cmd,
            [&](vk::CommandBuffer c, const std::vector<TerrainRVTManager::ScheduledBake>& bakes)
            {
                baker->begin(c, *pool,
                             terrain.pipeline->getWeightMapDescriptorSet(),
                             bindlessTextures->getDescriptorSet(),
                             terrain.pipeline->getTerrainDataDescriptorSet());

                const float borderFrac = static_cast<float>(vt::VT_BORDER) / static_cast<float>(vt::VT_PAGE_INTERIOR);

                for (const auto& b : bakes)
                {
                    baker->beginPage(c, *pool, b.tile);

                    // Expand the core page rect by the border margin so the full 128-texel tile
                    // (incl. its 4-texel border) samples correct neighbour content.
                    const glm::vec2 pageMin(b.worldRect.x, b.worldRect.y);
                    const glm::vec2 pageSize(b.worldRect.z, b.worldRect.w);
                    const glm::vec2 margin = pageSize * borderFrac;
                    const glm::vec2 expMin = pageMin - margin;
                    const glm::vec2 expSize = pageSize + margin * 2.0f;
                    const glm::vec2 expMax = expMin + expSize;

                    for (uint32_t ti = 0; ti < tiles.size(); ++ti)
                    {
                        const TerrainTileGPUData& t = tiles[ti];
                        const glm::vec2 tMin(t.aabbMin.x, t.aabbMin.z);
                        const glm::vec2 tMax(t.aabbMax.x, t.aabbMax.z);
                        const glm::vec2 qMin = glm::max(expMin, tMin);
                        const glm::vec2 qMax = glm::min(expMax, tMax);
                        if (qMin.x >= qMax.x || qMin.y >= qMax.y)
                            continue; // no overlap

                        TerrainRVTBaker::TilePush pc;
                        pc.pageWorldMin = expMin;
                        pc.pageWorldSize = expSize;
                        pc.quadWorldMin = qMin;
                        pc.quadWorldSize = qMax - qMin;
                        pc.tileWorldMin = tMin;
                        pc.tileWorldSize = glm::max(tMax.x - tMin.x, 1.0f);
                        pc.fragTileIndex = ti;
                        pc.textureScale = texScale;
                        baker->drawTile(c, pc);
                    }
                }
                baker->end(c);
            });
    }

    void GPUDrivenRenderer::copyTerrainRVTFeedback(vk::CommandBuffer cmd)
    {
        if (!terrainRVT)
            return;
        terrainRVT->copyFeedbackToStaging(cmd);
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

        auto materialData = resource::ResourceManager::loadTerrainMaterial(asset::AssetRef::fromPath(materialPath));
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

            auto tryRegisterLayerTex = [&](const std::string& texPath, vk::Format format = vk::Format::eR8G8B8A8Unorm) -> uint32_t
            {
                if (texPath.empty()) return 0;
                if (!materials.textureCache->loadTexture(texPath, format)) return 0;
                vk::ImageView view = materials.textureCache->getViewForPath(texPath);
                vk::Sampler sampler = materials.textureCache->getSamplerForPath(texPath);
                if (!view || !sampler) return 0;
                return bindlessTextures->registerTexture(texPath, view, sampler);
            };

            gpuLayer.albedoTextureIndex = tryRegisterLayerTex(layer.albedoTextureRef.resolve(), vk::Format::eR8G8B8A8Srgb);
            gpuLayer.normalTextureIndex = tryRegisterLayerTex(layer.normalTextureRef.resolve());
            gpuLayer.ormTextureIndex = tryRegisterLayerTex(layer.ormTextureRef.resolve());

            gpuLayer.tilingScale = layer.tilingScale;
            gpuLayer.roughness = layer.roughness;
            gpuLayer.metallic = layer.metallic;
            gpuLayer.ao = layer.ao;
            gpuLayer.emissionStrength = layer.emissionStrength;
        }

        {
            std::vector<std::string> texPaths;
            for (uint8_t i = 0; i < materialData->activeLayerCount; ++i)
            {
                const auto& layer = materialData->layers[i];
                texPaths.push_back(layer.albedoTextureRef.resolve());
                texPaths.push_back(layer.normalTextureRef.resolve());
                texPaths.push_back(layer.ormTextureRef.resolve());
            }
            registerTextureDependencies(materialPath, texPaths);
        }

        if (terrain.pipeline)
        {
            terrain.pipeline->updateTerrainLayerInfo(terrain.layerData);
        }

        terrain.currentMaterialPath = materialPath;
        terrain.layerDataDirty = false;
        rvtInvalidateAll = true; // VK-1209: terrain material changed -> re-bake resident RVT pages
        vfLogInfo("GPUDrivenRenderer: Registered {} terrain layer textures from '{}'",
                   materialData->activeLayerCount, materialPath);
    }

    void GPUDrivenRenderer::updateTerrain(const std::vector<terrain::TerrainTile*>& visibleTiles,
                                          const glm::vec3& cameraPosition,
                                          const std::string& terrainMaterialPath,
                                          const glm::vec2& terrainGridWorldMin,
                                          const glm::vec2& terrainGridWorldMax)
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

        // VK-1209: bring up the terrain RVT once bounds are known, then keep its sample resources
        // bound. Only active when config enabled RVT and the pipeline compiled RVT_ENABLED.
        if (vtCache.rvtEnabled && terrain.pipeline->isRVTSampleEnabled())
        {
            rvtWorldMin = terrainGridWorldMin;
            rvtWorldMax = terrainGridWorldMax;
            const bool boundsValid = (rvtWorldMax.x > rvtWorldMin.x) && (rvtWorldMax.y > rvtWorldMin.y);
            if (boundsValid && !terrainRVT)
            {
                terrainRVT = std::make_unique<TerrainRVTManager>(device);
                TerrainRVTManager::Config cfg;
                cfg.poolBudgetMB = vtCache.rvtPoolBudgetMB;
                cfg.texelsPerMeter = vtCache.rvtTexelsPerMeter;
                cfg.pagesPerFrame = vtCache.pagesPerFrame;
                cfg.evictionAgeFrames = vtCache.evictionAgeFrames;
                terrainRVT->init(cfg, rvtWorldMin, rvtWorldMax);

                if (const auto* pool = terrainRVT->getPool())
                {
                    struct alignas(16) RVTParamsCPU
                    {
                        vt::GPUVTImageInfo img;
                        float worldMinX, worldMinZ;
                        float invExtentX, invExtentZ;
                        float virtualResTexels;
                        float pad0, pad1, pad2;
                    } params{};
                    static_assert(sizeof(RVTParamsCPU) == 64, "RVTParams must match set-5 UBO (64 bytes)");
                    params.img = terrainRVT->getImageInfo();
                    params.worldMinX = rvtWorldMin.x;
                    params.worldMinZ = rvtWorldMin.y;
                    const glm::vec2 extent = glm::max(rvtWorldMax - rvtWorldMin, glm::vec2(1.0f));
                    params.invExtentX = 1.0f / extent.x;
                    params.invExtentZ = 1.0f / extent.y;
                    params.virtualResTexels = terrainRVT->virtualResTexelsX();
                    terrain.pipeline->updateRVTSampleResources(
                        terrainRVT->getPageTableBuffer(), pool->planeView(0), pool->planeView(1),
                        pool->getSampler(), terrainRVT->getFeedbackBuffer(), &params, sizeof(params));
                }
            }
            // Material change -> re-bake all resident fine pages.
            if (terrainRVT && rvtInvalidateAll)
            {
                terrainRVT->invalidateWorldRect(rvtWorldMin, rvtWorldMax);
                rvtInvalidateAll = false;
            }
        }

        terrain.updateUs = std::chrono::duration<float, std::micro>(uploadEnd - frameStart).count();
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
            for (uint32_t lod = 0; lod < TERRAIN_LOD_LEVEL_COUNT; ++lod)
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
        terrain.pipeline->setMeshletOcclusionCullingEnabled(culling.meshletOcclusionCullingEnabled);
        terrain.pipeline->setHiZMipLevels(prepassHiZMipLevels);

        uint32_t viewMode = culling.currentViewMode;
        if (culling.meshletFrustumCullingEnabled) viewMode |= TERRAIN_CULL_FRUSTUM_BIT;
        if (culling.meshletBackfaceCullingEnabled) viewMode |= TERRAIN_CULL_BACKFACE_BIT;

        terrain.pipeline->dispatch(
            cmd,
            currentImageIndex,
            viewMode,
            dispatchWidth,
            dispatchHeight,
            terrain.lodBias,
            terrain.errorThreshold,
            terrain.textureScale
        );
    }

    void GPUDrivenRenderer::setBrushOverlay(const glm::vec3& worldPos, float worldRadius, float falloff, float shape, float stampRotation)
    {
        if (terrain.pipeline)
        {
            terrain.pipeline->setBrushOverlay(worldPos, worldRadius, falloff, shape);
            terrain.pipeline->setStampRotation(stampRotation);
        }
    }

    void GPUDrivenRenderer::setStampOverlay(vk::Buffer buffer, uint32_t width, uint32_t height, float rotation)
    {
        if (terrain.pipeline)
        {
            terrain.pipeline->setStampOverlay(buffer, width, height, rotation);
        }
    }

    void GPUDrivenRenderer::clearStampOverlay()
    {
        if (terrain.pipeline)
        {
            terrain.pipeline->clearStampOverlay();
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

    void GPUDrivenRenderer::setTileLoadContextProvider(TerrainStreamManager::TileLoadContextProvider loader)
    {
        if (terrain.streamManager)
        {
            terrain.streamManager->setTileLoadContextProvider(std::move(loader));
        }
        else
        {
            terrain.pendingTileLoadContextProvider = std::move(loader);
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
