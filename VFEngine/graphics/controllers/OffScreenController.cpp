#include "OffScreenController.hpp"
#include "../core/VulkanContext.hpp"
#include "../render/OffScreenViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/billboard/BillboardPipeline.hpp"
#include "../render/DebugRenderer.hpp"
#include "offscreen/IBLController.hpp"
#include "offscreen/MeshAssetManager.hpp"
#include "offscreen/CameraController.hpp"
#include "offscreen/SceneBVHManager.hpp"
#include "offscreen/FramePreparationSystem.hpp"
#include "offscreen/CullingStatsCollector.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/MaterialEvents.hpp"

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
    }

    void OffScreenController::init()
    {
        offScreen->init();

        auto* renderHandler = offScreen->getRenderPassHandler();

        // Create extracted managers
        iblController = std::make_unique<offscreen::IBLController>(*renderHandler);
        meshAssetManager = std::make_unique<offscreen::MeshAssetManager>(*renderHandler);
        cameraController = std::make_unique<offscreen::CameraController>(*renderHandler);
        bvhManager = std::make_unique<offscreen::SceneBVHManager>();
        framePreparation = std::make_unique<offscreen::FramePreparationSystem>();
        statsCollector = std::make_unique<offscreen::CullingStatsCollector>();

        // Initialize BVH manager with render handler
        bvhManager->init(renderHandler);

        // Subscribe to material saved notifications for cache invalidation
        auto token = events::EventDispatcher::instance().subscribe<events::material::MaterialFileSavedNotification>(
            [this](const events::material::MaterialFileSavedNotification& notification)
            {
                framePreparation->invalidateMaterialCache(notification.materialPath);
            });
        materialSavedSubscription = std::make_unique<events::SubscriptionToken>(token);
    }

    void OffScreenController::recreate()
    {
        offScreen->recreate();
    }

    void OffScreenController::cleanUp() const
    {
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

    void OffScreenController::prepareFrameMeshes()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.cameraController = cameraController.get();
        ctx.playModeActive = playModeActive;
        ctx.showDebugRendering = showDebugRendering;
        ctx.showBillboardIcons = showBillboardIcons;
        ctx.showGrid = showGrid;

        framePreparation->prepareMeshes(ctx);
    }

    void OffScreenController::prepareFrameBillboards()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.cameraController = cameraController.get();
        ctx.playModeActive = playModeActive;
        ctx.showDebugRendering = showDebugRendering;
        ctx.showBillboardIcons = showBillboardIcons;
        ctx.showGrid = showGrid;

        framePreparation->prepareBillboards(ctx);
    }

    void OffScreenController::prepareFrameCameraFrustums()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.cameraController = cameraController.get();
        ctx.playModeActive = playModeActive;
        ctx.showDebugRendering = showDebugRendering;
        ctx.showBillboardIcons = showBillboardIcons;
        ctx.showGrid = showGrid;

        framePreparation->prepareCameraFrustums(ctx);
    }

    void OffScreenController::prepareFrameAudioSpheres()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.cameraController = cameraController.get();
        ctx.playModeActive = playModeActive;
        ctx.showDebugRendering = showDebugRendering;
        ctx.showBillboardIcons = showBillboardIcons;
        ctx.showGrid = showGrid;

        framePreparation->prepareAudioSpheres(ctx);
    }

    void OffScreenController::prepareGrid()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.cameraController = cameraController.get();
        ctx.playModeActive = playModeActive;
        ctx.showDebugRendering = showDebugRendering;
        ctx.showBillboardIcons = showBillboardIcons;
        ctx.showGrid = showGrid;

        framePreparation->prepareGrid(ctx);
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

    void OffScreenController::rebuildBVH()
    {
        bvhManager->rebuild();
    }

    void OffScreenController::markBVHDirty()
    {
        bvhManager->markDirty();
    }

    void OffScreenController::setOcclusionCullingEnabled(bool enabled)
    {
        cameraController->setOcclusionCullingEnabled(enabled);
    }

    bool OffScreenController::isOcclusionCullingEnabled() const
    {
        return cameraController->isOcclusionCullingEnabled();
    }

    void OffScreenController::createCamera(render::occlusion::CameraId id, bool enableOcclusion)
    {
        cameraController->create(id, enableOcclusion);
    }

    void OffScreenController::removeCamera(render::occlusion::CameraId id)
    {
        cameraController->remove(id);
    }

    void OffScreenController::setActiveCamera(render::occlusion::CameraId id)
    {
        cameraController->setActive(id);
    }

    render::occlusion::CameraId OffScreenController::getActiveCameraId() const
    {
        return cameraController->getActiveId();
    }

    void* OffScreenController::render()
    {
        return offScreen->render();
    }

    services::CullingDebugStats OffScreenController::getCullingStats() const
    {
        return statsCollector->collect(offScreen->getRenderPassHandler(), bvhManager.get());
    }

    void OffScreenController::setShowGrid(bool show)
    {
        showGrid = show;
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            if (show && renderHandler->isMeshPipelineInitialized() && !renderHandler->isDebugRendererInitialized())
            {
                renderHandler->initDebugRenderer();
            }

            if (renderHandler->isDebugRendererInitialized())
            {
                renderHandler->getDebugRenderer()->setShowGrid(show && !playModeActive);
            }
        }
    }

    void OffScreenController::setPlayMode(bool playMode)
    {
        playModeActive = playMode;

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler && renderHandler->isDebugRendererInitialized())
        {
            renderHandler->getDebugRenderer()->setShowGrid(showGrid && !playModeActive);
        }
    }

    void OffScreenController::setViewMode(uint32_t mode)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setViewMode(mode);
        }
    }

    uint32_t OffScreenController::getViewMode() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            return renderHandler->getViewMode();
        }
        return 0;
    }
}
