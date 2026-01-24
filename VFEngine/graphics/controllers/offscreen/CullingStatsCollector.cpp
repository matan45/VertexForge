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

        // Iterate over all registered cameras
        for (const auto& [cameraId, cameraData] : cameraManager->getAllCameras())
        {
            services::CameraCullingStats camStats;
            camStats.cameraId = cameraId;
            camStats.isActive = (cameraId == stats.activeCameraId);
            camStats.occlusionEnabled = cameraData->useOcclusionCulling;
            camStats.occlusionInitialized = cameraData->occlusionInitialized;
            camStats.frustumReady = cameraData->frustum.isInitialized();
            camStats.bvhBuilt = bvhManager->isBuilt();
            camStats.totalMeshEntities = totalMeshEntities;

            // Get visibility results for this camera if occlusion is active
            if (camStats.occlusionInitialized && cameraData->occlusionManager)
            {
                auto visibilityResults = cameraManager->getVisibilityResults(cameraId);
                if (!visibilityResults.empty())
                {
                    uint32_t visibleCount = 0;
                    for (uint32_t v : visibilityResults)
                    {
                        if (v != 0) ++visibleCount;
                    }
                    camStats.visibleAfterOcclusionCull = visibleCount;
                    camStats.occludedCount = static_cast<uint32_t>(visibilityResults.size()) - visibleCount;
                }
            }

            camStats.visibleAfterFrustumCull = camStats.visibleAfterOcclusionCull > 0
                                                   ? camStats.visibleAfterOcclusionCull + camStats.occludedCount
                                                   : totalMeshEntities;

            stats.cameraStats.push_back(camStats);
        }

        // Mesh BVH statistics
        stats.staticBvhEntityCount = bvhManager->getStaticEntityCount();
        stats.dynamicBvhEntityCount = bvhManager->getDynamicEntityCount();
        stats.staticBvhNodeCount = bvhManager->getStaticNodeCount();
        stats.dynamicBvhNodeCount = bvhManager->getDynamicNodeCount();

        // Light BVH statistics
        if (lightBvhManager)
        {
            stats.staticLightBvhCount = lightBvhManager->getStaticLightCount();
            stats.dynamicLightBvhCount = lightBvhManager->getDynamicLightCount();
            stats.staticLightBvhNodeCount = lightBvhManager->getStaticNodeCount();
            stats.dynamicLightBvhNodeCount = lightBvhManager->getDynamicNodeCount();
        }

        // GPU-driven rendering statistics
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

            // Meshlet culling stats (from task shader)
            stats.gpuDriven.meshletFrustumCullingEnabled = gpuDrivenRenderer->isMeshletFrustumCullingEnabled();
            stats.gpuDriven.meshletBackfaceCullingEnabled = gpuDrivenRenderer->isMeshletBackfaceCullingEnabled();
            auto meshletStats = gpuDrivenRenderer->getMeshletCullingStats();
            stats.gpuDriven.totalMeshlets = meshletStats.totalMeshlets;
            stats.gpuDriven.meshletsCulledByFrustum = meshletStats.culledByFrustum;
            stats.gpuDriven.meshletsCulledByBackface = meshletStats.culledByBackface;
            stats.gpuDriven.visibleMeshlets = meshletStats.visibleMeshlets;
        }

        return stats;
    }
}
