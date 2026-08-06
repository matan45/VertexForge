#include "OffScreenController.hpp"
#include "../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../render/gpudriven/brush/BrushComputePipeline.hpp"
#include "../render/gpudriven/brush/HydraulicErosionPipeline.hpp"
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
#include "../../services/events/editor/EditorModeEvents.hpp"
#include "../../services/events/scene/ScenePersistenceEvents.hpp"
#include "../../services/events/terrain/TerrainEvents.hpp"
#include "../../services/events/terrain/OceanEvents.hpp"
#include "../../services/events/scene/EntityTransformEvents.hpp"
#include "material/MaterialManager.hpp"

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
        if (editorModeChangedSubscription && editorModeChangedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*editorModeChangedSubscription);
        }
        if (sceneClearedSubscription && sceneClearedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*sceneClearedSubscription);
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
                if (!framePreparation) return;
                framePreparation->invalidateMaterialCache(notification.materialPath);
                // VK-1347: a saved PARENT material leaves derived instances' cached
                // (parent-inherited) PBR values stale. Invalidate each dependent instance.
                for (const auto& instancePath :
                     material::MaterialManager::instance().getInstancesOfParent(notification.materialPath))
                {
                    framePreparation->invalidateMaterialCache(instancePath);
                }
            });
        materialSavedSubscription = std::make_unique<events::SubscriptionToken>(token);

        // VK-1347: entering Play does not reload the scene, so no scene event fires.
        // Flush the entire PBR cache on the Edit->Play transition so material edits
        // made in Edit mode are picked up on the first Play frame.
        auto editorModeToken = events::EventDispatcher::instance().subscribe<
            events::editor::EditorModeChangedNotification>(
            [this](const events::editor::EditorModeChangedNotification& notification)
            {
                if (notification.currentMode == services::EditorMode::Play && framePreparation)
                {
                    framePreparation->clearAllMaterialCache();
                }
            });
        editorModeChangedSubscription = std::make_unique<events::SubscriptionToken>(editorModeToken);

        auto sceneClearedToken = events::EventDispatcher::instance().subscribe<
            events::scene::SceneClearedNotification>(
            [this](const events::scene::SceneClearedNotification&)
            {
                if (framePreparation)
                {
                    framePreparation->clearAllMaterialCache();
                }
            });
        sceneClearedSubscription = std::make_unique<events::SubscriptionToken>(sceneClearedToken);

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

        auto oceanToken = events::EventDispatcher::instance().subscribe<events::ocean::OceanDeletedNotification>(
            [this](const events::ocean::OceanDeletedNotification&)
            {
                auto* renderHandler = offScreen->getRenderPassHandler();
                if (renderHandler)
                {
                    renderHandler->clearWaterData();
                }
            });
        waterDeletedSubscription = std::make_unique<events::SubscriptionToken>(oceanToken);

        auto entitySelectedToken = events::EventDispatcher::instance().subscribe<events::scene::EntitySelectedNotification>(
            [this](const events::scene::EntitySelectedNotification& notification)
            {
                auto* rh = offScreen->getRenderPassHandler();
                if (!rh) return;

                if (!notification.entity.has_value())
                {
                    rh->clearSelectedTerrainTile();
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
                        return;
                    }
                }

                rh->clearSelectedTerrainTile();
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

        if (hydraulicErosionPipeline)
        {
            hydraulicErosionPipeline->cleanup();
            hydraulicErosionPipeline.reset();
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

    void OffScreenController::waitForIdle()
    {
        if (asyncComputeManager)
        {
            asyncComputeManager->waitIdle();
        }

        device.getLogicalDevice().waitIdle();
    }

    void OffScreenController::iblSet(std::string_view iblPath)
    {
        iblController->set(iblPath);
    }

    void OffScreenController::iblSetCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
    {
        iblController->setCameraMatrices(view, projection);
    }

    void OffScreenController::iblSetParams(float intensity, float rotationDeg, const glm::vec3& tint)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler) return;

        renderHandler->setIBLParams(intensity, rotationDeg, tint);
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
        retryPendingRenderSettings();
        return offScreen->render();
    }

    void* OffScreenController::render(const std::function<void()>& preRenderCallback)
    {
        retryPendingRenderSettings();
        return offScreen->render(preRenderCallback);
    }

    void* OffScreenController::getColorImage(uint32_t imageIndex) const
    {
        vk::Image img = offScreen->getColorImage(imageIndex);
        return static_cast<VkImage>(img);
    }
}
