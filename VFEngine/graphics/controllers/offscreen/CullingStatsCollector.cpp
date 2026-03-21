#include "CullingStatsCollector.hpp"
#include "SceneBVHManager.hpp"
#include "LightBVHManager.hpp"
#include "../../render/RenderPassHandler.hpp"
#include "../../render/occlusion/CameraOcclusionManager.hpp"
#include "../../render/gpudriven/GPUDrivenRenderer.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace controllers::offscreen
{
    services::CullingDebugStats CullingStatsCollector::collect(render::RenderPassHandler* renderHandler,
                                                                SceneBVHManager* bvhManager,
                                                                LightBVHManager* lightBvhManager) const
    {
        services::CullingDebugStats stats;

        auto* cameraManager = renderHandler->getCameraOcclusionManager();
        if (!cameraManager)
        {
            return stats;
        }

        stats.activeCameraId = cameraManager->getActiveCameraId();

        auto& registry = scene::EntityRegistry::getRegistry();
        uint32_t totalMeshEntities = static_cast<uint32_t>(registry.view<components::MeshComponent>().size());

        for (const auto& [cameraId, cameraData] : cameraManager->getAllCameras())
        {
            services::CameraCullingStats camStats;
            camStats.cameraId = cameraId;
            camStats.isActive = (cameraId == stats.activeCameraId);
            camStats.frustumReady = cameraData->frustum.isInitialized();
            camStats.bvhBuilt = bvhManager->isBuilt();
            camStats.totalMeshEntities = totalMeshEntities;

            stats.cameraStats.push_back(camStats);
        }

        stats.staticBvhEntityCount = bvhManager->getStaticEntityCount();
        stats.dynamicBvhEntityCount = bvhManager->getDynamicEntityCount();
        stats.staticBvhNodeCount = bvhManager->getStaticNodeCount();
        stats.dynamicBvhNodeCount = bvhManager->getDynamicNodeCount();

        if (lightBvhManager)
        {
            stats.staticLightBvhCount = lightBvhManager->getStaticLightCount();
            stats.dynamicLightBvhCount = lightBvhManager->getDynamicLightCount();
            stats.staticLightBvhNodeCount = lightBvhManager->getStaticNodeCount();
            stats.dynamicLightBvhNodeCount = lightBvhManager->getDynamicNodeCount();
        }

        auto* gpuDrivenRenderer = renderHandler->getGPUDrivenRenderer();
        if (gpuDrivenRenderer && renderHandler->isGPUDrivenRendererInitialized())
        {
            gpuDrivenRenderer->updateStatsFromGPU();

            stats.gpuDriven.enabled = gpuDrivenRenderer->isEnabled();
            stats.gpuDriven.frustumCullingEnabled = gpuDrivenRenderer->isFrustumCullingEnabled();
            stats.gpuDriven.occlusionCullingEnabled = gpuDrivenRenderer->isOcclusionCullingEnabled();
            stats.gpuDriven.lodSelectionEnabled = gpuDrivenRenderer->isLODSelectionEnabled();
            stats.gpuDriven.hiZMipLevels = gpuDrivenRenderer->getHiZMipLevels();

            const auto& gpuStats = gpuDrivenRenderer->getStats();
            stats.gpuDriven.totalObjects = gpuStats.totalObjects;
            stats.gpuDriven.visibleObjects = gpuStats.visibleObjects;
            stats.gpuDriven.culledByFrustum = gpuStats.culledByFrustum;
            stats.gpuDriven.culledByOcclusion = gpuStats.culledByOcclusion;
            stats.gpuDriven.culledByDistance = gpuStats.culledByDistance;
            stats.gpuDriven.objectsLOD0 = gpuStats.objectsLOD0;
            stats.gpuDriven.objectsLOD1 = gpuStats.objectsLOD1;
            stats.gpuDriven.objectsLOD2 = gpuStats.objectsLOD2;
            stats.gpuDriven.objectsLOD3 = gpuStats.objectsLOD3;

            stats.gpuDriven.mergedVertexCount = gpuDrivenRenderer->getMergedVertexCount();
            stats.gpuDriven.mergedIndexCount = gpuDrivenRenderer->getMergedIndexCount();
            stats.gpuDriven.registeredMeshCount = gpuDrivenRenderer->getRegisteredMeshCount();
            stats.gpuDriven.registeredTextureCount = gpuDrivenRenderer->getRegisteredTextureCount();

            stats.gpuDriven.batchCount = gpuDrivenRenderer->getBatchCount();
            stats.gpuDriven.commandsPerBatch = gpuDrivenRenderer->getCommandsPerBatch();
            stats.gpuDriven.totalCapacity = gpuDrivenRenderer->getTotalCapacity();
            stats.gpuDriven.drawCalls = gpuStats.drawCalls;

            stats.gpuDriven.drawCommandBufferSize = gpuDrivenRenderer->getDrawCommandBufferSize();
            stats.gpuDriven.drawCountBufferSize = gpuDrivenRenderer->getDrawCountBufferSize();
            stats.gpuDriven.perDrawDataBufferSize = gpuDrivenRenderer->getPerDrawDataBufferSize();
            stats.gpuDriven.totalMemoryUsage = gpuDrivenRenderer->getTotalMemoryUsage();

            stats.gpuDriven.meshletFrustumCullingEnabled = gpuDrivenRenderer->isMeshletFrustumCullingEnabled();
            stats.gpuDriven.meshletBackfaceCullingEnabled = gpuDrivenRenderer->isMeshletBackfaceCullingEnabled();
            stats.gpuDriven.meshletOcclusionCullingEnabled = gpuDrivenRenderer->isMeshletOcclusionCullingEnabled();
            auto meshletStats = gpuDrivenRenderer->getMeshletCullingStats();
            stats.gpuDriven.totalMeshlets = meshletStats.totalMeshlets;
            stats.gpuDriven.meshletsCulledByFrustum = meshletStats.culledByFrustum;
            stats.gpuDriven.meshletsCulledByBackface = meshletStats.culledByBackface;
            stats.gpuDriven.meshletsCulledByOcclusion = meshletStats.culledByOcclusion;
            stats.gpuDriven.visibleMeshlets = meshletStats.visibleMeshlets;

            stats.terrain.updateTerrainUs = gpuDrivenRenderer->getTerrainUpdateUs();
            stats.terrain.streamingUs = gpuDrivenRenderer->getTerrainStreamingUs();
            stats.terrain.buildTileDataUs = gpuDrivenRenderer->getTerrainBuildTileDataUs();
            stats.terrain.uploadTileDataUs = gpuDrivenRenderer->getTerrainUploadTileDataUs();

            auto terrainCulling = gpuDrivenRenderer->getTerrainCullingStats();
            stats.terrain.totalTiles = terrainCulling.totalTiles;
            stats.terrain.culledTiles = terrainCulling.culledTiles;
            stats.terrain.totalMeshlets = terrainCulling.totalMeshlets;
            stats.terrain.culledMeshlets = terrainCulling.culledMeshlets;
            stats.terrain.culledByOcclusion = terrainCulling.culledByOcclusion;
            stats.terrain.visibleMeshlets = terrainCulling.visibleMeshlets;
            stats.terrain.lodCount0 = terrainCulling.lodCount0;
            stats.terrain.lodCount1 = terrainCulling.lodCount1;
            stats.terrain.lodCount2 = terrainCulling.lodCount2;
            stats.terrain.lodCount3 = terrainCulling.lodCount3;
            stats.terrain.lodCount4 = terrainCulling.lodCount4;
            stats.terrain.lodCount5 = terrainCulling.lodCount5;

            const auto* streamStats = gpuDrivenRenderer->getTerrainStreamingStats();
            if (streamStats)
            {
                stats.terrain.tilesLoaded = streamStats->tilesLoaded;
                stats.terrain.tilesStreaming = streamStats->tilesStreaming;
                stats.terrain.fallbackTiles = streamStats->fallbackTiles;
                stats.terrain.fullDetailTiles = streamStats->fullDetailTiles;
                stats.terrain.uploadsThisFrame = streamStats->uploadsThisFrame;
                stats.terrain.memoryUsedBytes = streamStats->memoryUsedBytes;
                stats.terrain.memoryBudgetBytes = streamStats->memoryBudgetBytes;
                stats.terrain.bytesUploadedThisFrame = streamStats->bytesUploadedThisFrame;
            }

            const auto* texStreamStats = gpuDrivenRenderer->getTextureStreamStats();
            if (texStreamStats)
            {
                stats.textureStream.totalRegistered = texStreamStats->totalRegistered;
                stats.textureStream.fullyLoaded = texStreamStats->fullyLoaded;
                stats.textureStream.partiallyLoaded = texStreamStats->partiallyLoaded;
                stats.textureStream.pendingReads = texStreamStats->pendingReads;
                stats.textureStream.pendingUploads = texStreamStats->pendingUploads;
                stats.textureStream.uploadsThisFrame = texStreamStats->uploadsThisFrame;
                stats.textureStream.bytesUploadedThisFrame = texStreamStats->bytesUploadedThisFrame;
                stats.textureStream.vramUsedBytes = texStreamStats->vramUsedBytes;
                stats.textureStream.vramBudgetBytes = texStreamStats->vramBudgetBytes;
                stats.textureStream.evictionsThisFrame = texStreamStats->evictionsThisFrame;
            }

            stats.water.readbackUs = gpuDrivenRenderer->getWaterReadbackUs();
            stats.water.dispatchUs = gpuDrivenRenderer->getWaterDispatchUs();
            stats.water.updateUs = gpuDrivenRenderer->getWaterUpdateUs();
            stats.water.renderUs = gpuDrivenRenderer->getWaterRenderUs();

            stats.gpuDriven.bvhLightCullingEnabled = gpuDrivenRenderer->isBVHLightCullingEnabled();
            stats.gpuDriven.hiZLightOcclusionEnabled = gpuDrivenRenderer->isLightOcclusionCullingEnabled();
            stats.gpuDriven.totalLights = gpuDrivenRenderer->getTotalSceneLights();
            stats.gpuDriven.lightsAfterBVHCull = gpuDrivenRenderer->getLightsAfterBVHCull();
            stats.gpuDriven.lightsAfterHiZCull = gpuDrivenRenderer->getLightsAfterHiZCull();

            if (stats.gpuDriven.lightsAfterBVHCull <= stats.gpuDriven.totalLights)
                stats.gpuDriven.lightsCulledByBVH = stats.gpuDriven.totalLights - stats.gpuDriven.lightsAfterBVHCull;
            else
                stats.gpuDriven.lightsCulledByBVH = 0;

            if (stats.gpuDriven.lightsAfterHiZCull <= stats.gpuDriven.lightsAfterBVHCull)
                stats.gpuDriven.lightsCulledByHiZ = stats.gpuDriven.lightsAfterBVHCull - stats.gpuDriven.lightsAfterHiZCull;
            else
                stats.gpuDriven.lightsCulledByHiZ = 0;
        }

        return stats;
    }
}
