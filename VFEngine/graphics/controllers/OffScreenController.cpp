#include "OffScreenController.hpp"
#include "../render/gpudriven/BrushComputePipeline.hpp"
#include "../core/VulkanContext.hpp"
#include "../render/OffScreenViewPort.hpp"
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
#include "../../services/events/MaterialEvents.hpp"
#include "../../services/events/TerrainEvents.hpp"
#include "../../services/events/WaterEvents.hpp"

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
        if (waterDeletedSubscription && waterDeletedSubscription->isValid())
        {
            events::EventDispatcher::instance().unsubscribe(*waterDeletedSubscription);
        }
    }

    void OffScreenController::init()
    {
        offScreen->init();

        auto* renderHandler = offScreen->getRenderPassHandler();

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
}
