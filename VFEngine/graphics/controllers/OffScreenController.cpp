#include "OffScreenController.hpp"
#include "../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../render/gpudriven/brush/BrushComputePipeline.hpp"
#include "../core/VulkanContext.hpp"
#include "../core/AsyncComputeManager.hpp"
#include "../core/ThreadCommandPoolManager.hpp"
#include "../render/OffScreenViewPort.hpp"
#include "threading/JobSystem.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/billboard/BillboardPipeline.hpp"
#include "offscreen/IBLController.hpp"
#include "offscreen/MeshAssetManager.hpp"
#include "offscreen/CameraController.hpp"
#include "offscreen/SceneBVHManager.hpp"
#include "offscreen/LightBVHManager.hpp"
#include "offscreen/FramePreparationSystem.hpp"
#include "offscreen/CullingStatsCollector.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/render/MaterialEvents.hpp"
#include "../../services/events/terrain/TerrainEvents.hpp"
#include "../../services/events/terrain/WaterEvents.hpp"
#include "../../services/events/scene/EntityTransformEvents.hpp"

namespace controllers
{
    OffScreenController::OffScreenController()
        : swapChain{*core::VulkanContext::getSwapChain()}
          , device{*core::VulkanContext::getDevice()}
          , offScreen{std::make_unique<render::OffScreenViewPort>(device, swapChain)}
    {
    }

    OffScreenController::~OffScreenController()
    {
        if (materialSavedSubscription && materialSavedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*materialSavedSubscription);
        }
        if (terrainDeletedSubscription && terrainDeletedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*terrainDeletedSubscription);
        }
        if (tileRemovedSubscription && tileRemovedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*tileRemovedSubscription);
        }
        if (waterDeletedSubscription && waterDeletedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*waterDeletedSubscription);
        }
        if (entitySelectedSubscription && entitySelectedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*entitySelectedSubscription);
        }
    }

    void OffScreenController::init()
    {
        // Initialize async compute manager before offscreen viewport
        asyncComputeManager = std::make_unique<core::AsyncComputeManager>(device);
        asyncComputeManager->init();

        offScreen->init();

        // Wire async compute to the viewport
        offScreen->setAsyncComputeManager(asyncComputeManager.get());

        // Per-thread command pools for parallel scene recording (mesh/terrain/grass/water groups).
        // These are separate from ShadowSystem's ThreadCommandPoolManager which manages
        // its own pools for VSM tile recording. Both use the same JobSystem thread pool
        // but never overlap (shadow recording completes before scene recording starts).
        sceneThreadPoolManager = std::make_unique<core::ThreadCommandPoolManager>();
        // 5 slots: mesh(0), terrain(1), grass(2), water+billboards(3), overlays(4)
        uint32_t sceneThreads = std::max(5u, threading::JobSystem::instance().getThreadCount());
        sceneThreadPoolManager->init(device, sceneThreads);

        auto* renderHandler = offScreen->getRenderPassHandler();

        // Enable parallel scene recording
        renderHandler->setParallelSceneRecording(true, sceneThreadPoolManager.get());

        iblController = std::make_unique<offscreen::IBLController>(*renderHandler);
        meshAssetManager = std::make_unique<offscreen::MeshAssetManager>(*renderHandler);
        cameraController = std::make_unique<offscreen::CameraController>(*renderHandler);
        bvhManager = std::make_unique<offscreen::SceneBVHManager>();
        lightBvhManager = std::make_unique<offscreen::LightBVHManager>();
        framePreparation = std::make_unique<offscreen::FramePreparationSystem>();
        statsCollector = std::make_unique<offscreen::CullingStatsCollector>();

        bvhManager->init(renderHandler);
        lightBvhManager->init();

        auto token = events::EventDispatcher::instance().subscribe<events::material::MaterialFileSavedNotification>(
            [this](const events::material::MaterialFileSavedNotification& notification)
            {
                framePreparation->invalidateMaterialCache(notification.materialPath);
            });
        materialSavedSubscription = std::make_unique<events::SubscriptionToken>(token);

        auto terrainToken = events::EventDispatcher::instance().subscribe<events::terrain::TerrainDeletedNotification>(
            [this](const events::terrain::TerrainDeletedNotification&)
            {
                auto* renderHandler = offScreen->getRenderPassHandler();
                if (renderHandler)
                {
                    renderHandler->clearTerrainData();
                }
            });
        terrainDeletedSubscription = std::make_unique<events::SubscriptionToken>(terrainToken);

        auto tileRemovedToken = events::EventDispatcher::instance().subscribe<events::terrain::TerrainTileRemovedNotification>(
            [this](const events::terrain::TerrainTileRemovedNotification& notification)
            {
                auto* renderHandler = offScreen->getRenderPassHandler();
                if (renderHandler)
                {
                    renderHandler->evictTerrainTile(notification.tileX, notification.tileZ);
                }
            });
        tileRemovedSubscription = std::make_unique<events::SubscriptionToken>(tileRemovedToken);

        auto waterToken = events::EventDispatcher::instance().subscribe<events::water::WaterDeletedNotification>(
            [this](const events::water::WaterDeletedNotification&)
            {
                auto* renderHandler = offScreen->getRenderPassHandler();
                if (renderHandler)
                {
                    renderHandler->clearWaterData();
                }
            });
        waterDeletedSubscription = std::make_unique<events::SubscriptionToken>(waterToken);

        auto entitySelectedToken = events::EventDispatcher::instance().subscribe<events::scene::EntitySelectedNotification>(
            [this](const events::scene::EntitySelectedNotification& notification)
            {
                auto* rh = offScreen->getRenderPassHandler();
                if (!rh) return;

                if (!notification.entity.has_value())
                {
                    rh->clearSelectedTerrainTile();
                    rh->clearSelectedWaterTile();
                    return;
                }

                auto& disp = events::EventDispatcher::instance();

                events::terrain::HasTerrainTileComponentQuery tileQuery;
                tileQuery.entity = *notification.entity;
                if (disp.query(tileQuery))
                {
                    events::terrain::GetTerrainTileDataQuery dataQuery;
                    dataQuery.entity = *notification.entity;
                    auto tileOpt = disp.query(dataQuery);
                    if (tileOpt.has_value())
                    {
                        rh->setSelectedTerrainTile(tileOpt->tileX, tileOpt->tileZ);
                        rh->clearSelectedWaterTile();
                        return;
                    }
                }

                events::water::HasWaterTileComponentQuery waterTileQuery;
                waterTileQuery.entity = *notification.entity;
                if (disp.query(waterTileQuery))
                {
                    events::water::GetWaterTileDataQuery waterDataQuery;
                    waterDataQuery.entity = *notification.entity;
                    auto waterTileOpt = disp.query(waterDataQuery);
                    if (waterTileOpt.has_value())
                    {
                        rh->setSelectedWaterTile(waterTileOpt->tileX, waterTileOpt->tileZ);
                        rh->clearSelectedTerrainTile();
                        return;
                    }
                }

                rh->clearSelectedTerrainTile();
                rh->clearSelectedWaterTile();
            });
        entitySelectedSubscription = std::make_unique<events::SubscriptionToken>(entitySelectedToken);
    }

    void OffScreenController::recreate()
    {
        offScreen->recreate();
    }

    void OffScreenController::cleanUp()
    {
        if (brushComputePipeline)
        {
            brushComputePipeline->cleanup();
            brushComputePipeline.reset();
        }

        offScreen->cleanUp();

        if (sceneThreadPoolManager)
        {
            sceneThreadPoolManager->cleanUp();
            sceneThreadPoolManager.reset();
        }

        if (asyncComputeManager)
        {
            asyncComputeManager->cleanUp();
            asyncComputeManager.reset();
        }
    }

    void OffScreenController::iblSet(std::string_view iblPath)
    {
        iblController->set(iblPath);
    }

    void OffScreenController::iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
    {
        iblController->setCameraMatrices(view, projection);
    }

    void OffScreenController::iblRemove()
    {
        iblController->remove();
    }

    std::string OffScreenController::meshLoad(std::string_view meshPath)
    {
        return meshAssetManager->load(meshPath);
    }

    void OffScreenController::meshUnload(const std::string& meshId)
    {
        meshAssetManager->unload(meshId);
    }

    void OffScreenController::meshRelease(const std::string& meshPath)
    {
        auto* handler = getRenderPassHandler();
        if (handler)
        {
            auto* gpu = handler->getGPUDrivenRenderer();
            if (gpu)
            {
                gpu->releaseMeshAsset(meshPath);
            }
        }
    }

    void OffScreenController::textureRelease(const std::string& texturePath)
    {
        auto* handler = getRenderPassHandler();
        if (handler)
        {
            auto* gpu = handler->getGPUDrivenRenderer();
            if (gpu)
            {
                gpu->releaseTextureAsset(texturePath);
            }
        }
    }

    void OffScreenController::materialRelease(const std::string& materialPath)
    {
        auto* handler = getRenderPassHandler();
        if (handler)
        {
            auto* gpu = handler->getGPUDrivenRenderer();
            if (gpu)
            {
                gpu->releaseMaterialAsset(materialPath);
            }
        }
    }

    void OffScreenController::meshUpdateCamera(render::occlusion::CameraId cameraId,
                                               const glm::mat4& view, const glm::mat4& projection,
                                               const glm::vec3& cameraPos, float time)
    {
        cameraController->updateCamera(cameraId, view, projection, cameraPos, time);
    }

    bool OffScreenController::isMeshLoaded(const std::string& meshPath) const
    {
        return meshAssetManager->isLoaded(meshPath);
    }

    std::vector<std::string> OffScreenController::getLoadedMeshes() const
    {
        return meshAssetManager->getLoadedMeshes();
    }

    std::optional<services::MeshBounds> OffScreenController::getMeshBoundingBox(const std::string& meshPath) const
    {
        return meshAssetManager->getBoundingBox(meshPath);
    }

    void OffScreenController::prepareCameras()
    {
        cameraController->prepareCameras();
    }

    bool OffScreenController::loadBillboardAtlas(const std::string& atlasPath)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        renderHandler->initBillboardPipeline();

        auto* billboardPipeline = renderHandler->getBillboardPipeline();
        if (!billboardPipeline)
        {
            return false;
        }

        return billboardPipeline->loadAtlas(atlasPath);
    }

    void OffScreenController::setOcclusionCullingEnabled(bool enabled)
    {
        cameraController->setOcclusionCullingEnabled(enabled);

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setOcclusionCullingEnabled(enabled);
        }
    }

    void OffScreenController::removeCamera(render::occlusion::CameraId id)
    {
        cameraController->remove(id);
    }

    void* OffScreenController::render()
    {
        return offScreen->render();
    }

    void* OffScreenController::getColorImage(uint32_t imageIndex) const
    {
        vk::Image img = offScreen->getColorImage(imageIndex);
        return static_cast<VkImage>(img);
    }
}
