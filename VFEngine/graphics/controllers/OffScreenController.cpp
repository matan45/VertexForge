#include "OffScreenController.hpp"
#include "../render/gpudriven/BrushComputePipeline.hpp"
#include "../core/VulkanContext.hpp"
#include "../render/OffScreenViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/billboard/BillboardPipeline.hpp"
#include "../render/DebugRenderer.hpp"
#include "../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../render/shadow/ShadowSystem.hpp"
#include "../render/postprocess/PostProcessPipeline.hpp"
#include "../render/tools/ShadowDebugRenderer.hpp"
#include "types/RenderSettings.hpp"
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
#include "time/Timer.hpp"

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

    void OffScreenController::prepareFrameMeshes()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.lightBvhManager = lightBvhManager.get();
        ctx.cameraController = cameraController.get();
        ctx.playModeActive = playModeActive;
        ctx.showDebugRendering = showDebugRendering;
        ctx.showBillboardIcons = showBillboardIcons;
        ctx.showGrid = showGrid;
        ctx.deltaTime = static_cast<float>(engineTime::Timer::getDeltaTime());

        framePreparation->prepareMeshes(ctx);
    }

    void OffScreenController::prepareFrameBillboards()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.lightBvhManager = lightBvhManager.get();
        ctx.cameraController = cameraController.get();
        ctx.playModeActive = playModeActive;
        ctx.showDebugRendering = showDebugRendering;
        ctx.showBillboardIcons = showBillboardIcons;
        ctx.showGrid = showGrid;

        framePreparation->prepareBillboards(ctx);
    }

    void OffScreenController::prepareFrameText()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.lightBvhManager = lightBvhManager.get();
        ctx.cameraController = cameraController.get();
        ctx.playModeActive = playModeActive;

        framePreparation->prepareText(ctx);
    }

    void OffScreenController::prepareFrameCameraFrustums()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.lightBvhManager = lightBvhManager.get();
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
        ctx.lightBvhManager = lightBvhManager.get();
        ctx.cameraController = cameraController.get();
        ctx.playModeActive = playModeActive;
        ctx.showDebugRendering = showDebugRendering;
        ctx.showBillboardIcons = showBillboardIcons;
        ctx.showGrid = showGrid;

        framePreparation->prepareAudioSpheres(ctx);
    }

    void OffScreenController::prepareFrameLightGizmos()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.lightBvhManager = lightBvhManager.get();
        ctx.cameraController = cameraController.get();
        ctx.playModeActive = playModeActive;
        ctx.showDebugRendering = showDebugRendering;
        ctx.showBillboardIcons = showBillboardIcons;
        ctx.showGrid = showGrid;

        framePreparation->prepareLightGizmos(ctx);
    }

    void OffScreenController::prepareGrid()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.lightBvhManager = lightBvhManager.get();
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

    services::CullingDebugStats OffScreenController::getCullingStats() const
    {
        return statsCollector->collect(offScreen->getRenderPassHandler(), bvhManager.get(), lightBvhManager.get());
    }

    void OffScreenController::applyShadowSettings(const types::RenderSettings& settings)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler)
            return;

        auto* gpuDriven = renderHandler->getGPUDrivenRenderer();
        if (!gpuDriven)
            return;

        auto* shadowSystem = gpuDriven->getShadowSystem();
        if (shadowSystem)
        {
            shadowSystem->applyRenderSettings(settings);
        }

        auto* lightBufferManager = gpuDriven->getLightBufferManager();
        if (lightBufferManager)
        {
            lightBufferManager->setShadowIntensity(settings.shadows.shadowIntensity);
        }

        gpuDriven->setFrustumCullingEnabled(settings.culling.frustumCullingEnabled);
        gpuDriven->setOcclusionCullingEnabled(settings.culling.occlusionCullingEnabled);
        gpuDriven->setLODSelectionEnabled(settings.culling.lodSelectionEnabled);
        gpuDriven->setMeshletFrustumCullingEnabled(settings.culling.meshletFrustumCullingEnabled);
        gpuDriven->setMeshletBackfaceCullingEnabled(settings.culling.meshletBackfaceCullingEnabled);
        gpuDriven->setTerrainFrustumCullingEnabled(settings.culling.terrainFrustumCullingEnabled);
        gpuDriven->setTerrainMeshletCullingEnabled(settings.culling.terrainMeshletCullingEnabled);

        gpuDriven->setTerrainRenderingEnabled(settings.terrain.enabled);
        gpuDriven->setTerrainLODBias(settings.terrain.lodBias);
        gpuDriven->setTerrainErrorThreshold(settings.terrain.errorThreshold);
        gpuDriven->setTerrainTextureScale(settings.terrain.textureScale);
        gpuDriven->setTerrainShadowLOD(settings.terrain.shadowLOD);
    }

    services::ShadowStats OffScreenController::getShadowStats() const
    {
        services::ShadowStats stats{};

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler)
            return stats;

        auto* gpuDriven = renderHandler->getGPUDrivenRenderer();
        if (!gpuDriven)
            return stats;

        auto* shadowSystem = gpuDriven->getShadowSystem();
        if (!shadowSystem)
            return stats;

        auto* atlasManager = shadowSystem->getAtlasManager();
        if (atlasManager)
        {
            stats.atlasWidth = atlasManager->getAtlasWidth();
            stats.atlasHeight = atlasManager->getAtlasHeight();
            stats.atlasUtilization = atlasManager->getAtlasUtilization();
        }

        stats.activeShadowCasters = shadowSystem->getActiveShadowCasterCount();
        stats.activeShadowViews = shadowSystem->getActiveShadowViewCount();

        stats.directionalLightCount = static_cast<uint32_t>(shadowSystem->getDirectionalShadowViews().size());
        stats.pointLightCount = static_cast<uint32_t>(shadowSystem->getPointShadowViews().size());
        stats.spotLightCount = static_cast<uint32_t>(shadowSystem->getSpotShadowViews().size());

        auto quality = static_cast<types::ShadowQuality>(shadowSystem->getGlobalQuality());
        auto atlasConfig = types::ShadowAtlasConfig::fromQuality(quality);
        stats.pointResolution = atlasConfig.pointResolution;

        return stats;
    }

    void OffScreenController::applyPostProcessSettings(const postprocess::PostProcessSettings& settings)
    {
        currentPostProcessSettings = settings;

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler)
            return;

        auto* pipeline = renderHandler->getPostProcessPipeline();
        if (pipeline)
        {
            pipeline->applySettings(settings);
        }
    }

    postprocess::PostProcessSettings OffScreenController::getPostProcessSettings() const
    {
        return currentPostProcessSettings;
    }

    void OffScreenController::setPostProcessEnabled(bool enabled)
    {
        currentPostProcessSettings.enabled = enabled;

        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler)
            return;

        auto* pipeline = renderHandler->getPostProcessPipeline();
        if (pipeline)
        {
            pipeline->applySettings(currentPostProcessSettings);
        }
    }

    bool OffScreenController::isPostProcessEnabled() const
    {
        return currentPostProcessSettings.enabled;
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

    void OffScreenController::setShowPhysicsDebug(bool show)
    {
        showPhysicsDebug = show;
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            if (show && renderHandler->isMeshPipelineInitialized() && !renderHandler->isDebugRendererInitialized())
            {
                renderHandler->initDebugRenderer();
            }

            if (renderHandler->isDebugRendererInitialized())
            {
                renderHandler->setShowPhysicsDebug(show);
            }
        }
    }

    void OffScreenController::prepareFramePhysicsColliders()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.lightBvhManager = lightBvhManager.get();
        ctx.cameraController = cameraController.get();
        ctx.playModeActive = playModeActive;
        ctx.showDebugRendering = showDebugRendering;
        ctx.showBillboardIcons = showBillboardIcons;
        ctx.showGrid = showGrid;
        ctx.showPhysicsDebug = showPhysicsDebug;

        framePreparation->preparePhysicsColliders(ctx);
    }

    void OffScreenController::prepareFrameClusterDebug()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.lightBvhManager = lightBvhManager.get();
        ctx.cameraController = cameraController.get();
        ctx.playModeActive = playModeActive;
        ctx.showDebugRendering = showDebugRendering;
        ctx.showBillboardIcons = showBillboardIcons;
        ctx.showGrid = showGrid;
        ctx.showPhysicsDebug = showPhysicsDebug;
        ctx.showClusterDebug = showClusterDebug;

        framePreparation->prepareClusterDebug(ctx);
    }

    void OffScreenController::prepareFrameShadowDebug()
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (!renderHandler)
            return;

        if (!showShadowDebug || playModeActive)
        {
            if (renderHandler->isDebugRendererInitialized())
            {
                auto* debugRenderer = renderHandler->getDebugRenderer();
                if (debugRenderer)
                {
                    debugRenderer->setShowShadowDebug(false);
                }
            }
            return;
        }

        if (!renderHandler->isDebugRendererInitialized())
        {
            if (renderHandler->isMeshPipelineInitialized())
            {
                renderHandler->initDebugRenderer();
            }
            else
            {
                return;
            }
        }

        auto* debugRenderer = renderHandler->getDebugRenderer();
        if (!debugRenderer)
            return;

        auto* gpuDriven = renderHandler->getGPUDrivenRenderer();
        if (!gpuDriven)
            return;

        auto* shadowSystem = gpuDriven->getShadowSystem();
        if (!shadowSystem || !shadowSystem->isInitialized())
            return;

        auto shadowInfos = shadowSystem->getShadowDebugInfo();
        std::vector<render::mesh::ShadowFrustumRenderData> shadowDrawList;
        shadowDrawList.reserve(shadowInfos.size());

        for (const auto& info : shadowInfos)
        {
            render::mesh::ShadowFrustumRenderData renderData;

            switch (info.type)
            {
                case render::shadow::ShadowMapType::DirectionalCSM:
                case render::shadow::ShadowMapType::Directional2D:
                    renderData.type = render::mesh::ShadowFrustumType::DirectionalCascade;
                    renderData.cascadeIndex = info.cascadeIndex;
                    renderData.inverseViewProjection = glm::inverse(info.viewProjectionMatrix);
                    break;

                case render::shadow::ShadowMapType::Spot2D:
                    renderData.type = render::mesh::ShadowFrustumType::SpotFrustum;
                    renderData.inverseViewProjection = glm::inverse(info.viewProjectionMatrix);
                    break;

                case render::shadow::ShadowMapType::PointCube:
                    renderData.type = render::mesh::ShadowFrustumType::PointSphere;
                    renderData.lightPosition = info.lightPosition;
                    renderData.radius = info.farPlane;
                    break;

                default:
                    continue;
            }

            shadowDrawList.push_back(renderData);
        }

        debugRenderer->setShadowFrustumDrawList(std::move(shadowDrawList));
        debugRenderer->setShowShadowDebug(showShadowDebug);
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

    void OffScreenController::setFrustumCullingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setFrustumCullingEnabled(enabled);
        }
    }

    void OffScreenController::setLODSelectionEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setLODSelectionEnabled(enabled);
        }
    }

    void OffScreenController::setMeshletFrustumCullingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setMeshletFrustumCullingEnabled(enabled);
        }
    }

    void OffScreenController::setMeshletBackfaceCullingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setMeshletBackfaceCullingEnabled(enabled);
        }
    }

    void OffScreenController::setTerrainFrustumCullingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainFrustumCullingEnabled(enabled);
        }
    }

    void OffScreenController::setTerrainMeshletCullingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainMeshletCullingEnabled(enabled);
        }
    }

    void OffScreenController::setTerrainRenderingEnabled(bool enabled)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainRenderingEnabled(enabled);
        }
    }

    void OffScreenController::setTerrainLODBias(float bias)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainLODBias(bias);
        }
    }

    void OffScreenController::setTerrainErrorThreshold(float threshold)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainErrorThreshold(threshold);
        }
    }

    void OffScreenController::setTerrainTextureScale(float scale)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainTextureScale(scale);
        }
    }

    void OffScreenController::setTerrainShadowLOD(uint32_t lod)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainShadowLOD(lod);
        }
    }

    void OffScreenController::setVFXRuntimeProvider(services::IVFXRuntimeProvider* provider)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setVFXRuntimeProvider(provider);
        }
    }

    void OffScreenController::setTerrainRenderProvider(services::ITerrainRenderProvider* provider)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            renderHandler->setTerrainRenderProvider(provider);
        }
    }

    void OffScreenController::setRaycastCursorUV(const glm::vec2& uv)
    {
        offScreen->setRaycastCursorUV(uv);
    }

    void OffScreenController::clearRaycastCursor()
    {
        offScreen->clearRaycastCursor();
    }

    terrain::TerrainHitResult OffScreenController::getTerrainHitResult() const
    {
        return offScreen->getTerrainHitResult();
    }

    void OffScreenController::setBrushOverlayParams(float radius, float falloff, float shape)
    {
        offScreen->setBrushOverlayParams(radius, falloff, shape);
    }

    bool OffScreenController::applyBrushGPU(
        std::vector<float>& heightData,
        const terrain::BrushGPUParams& params)
    {
        if (!brushComputePipeline)
        {
            brushComputePipeline = std::make_unique<render::gpudriven::BrushComputePipeline>(device);
            brushComputePipeline->init();
        }

        render::gpudriven::BrushComputePushConstants constants{};
        constants.brushCenter = params.brushCenter;
        constants.tileWorldOrigin = params.tileWorldOrigin;
        constants.brushRadius = params.brushRadius;
        constants.brushStrength = params.brushStrength;
        constants.vertexSpacing = params.vertexSpacing;
        constants.verticesPerSide = params.verticesPerSide;
        constants.falloffType = static_cast<uint32_t>(params.falloff);
        constants.shapeType = static_cast<uint32_t>(params.shape);
        constants.brushType = static_cast<uint32_t>(params.brushType);
        constants.deltaTime = params.deltaTime;
        constants.targetHeight = params.targetHeight;
        constants.minHeight = params.minHeight;
        constants.maxHeight = params.maxHeight;
        constants.invertFlag = params.invert ? 1u : 0u;

        return brushComputePipeline->applyBrush(heightData, constants);
    }
}
