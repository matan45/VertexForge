#include "OffScreenController.hpp"
#include "../core/SwapChain.hpp"
#include "../render/OffScreenViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/billboard/BillboardPipeline.hpp"
#include "../render/DebugRenderer.hpp"
#include "../render/gpudriven/GPUDrivenRenderer.hpp"
#include "../render/shadow/ShadowSystem.hpp"
#include "../render/tools/ShadowDebugRenderer.hpp"
#include "offscreen/CameraController.hpp"
#include "offscreen/SceneBVHManager.hpp"
#include "offscreen/LightBVHManager.hpp"
#include "offscreen/FramePreparationSystem.hpp"
#include "../../services/events/EventDispatcher.hpp"
#include "../../services/events/input/InputEvents.hpp"
#include "time/Timer.hpp"

namespace controllers
{
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

    void OffScreenController::prepareSceneData()
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

        framePreparation->prepareSceneData(ctx);
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
        framePreparation->prepareReverbZones(ctx);
        framePreparation->prepareFogVolumes(ctx);
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

    void OffScreenController::prepareFrameUICanvasOutlines()
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

        framePreparation->prepareUICanvasOutlines(ctx);
    }

    void OffScreenController::prepareFrameUIImages()
    {
        offscreen::FrameContext ctx;
        ctx.renderHandler = offScreen->getRenderPassHandler();
        ctx.bvhManager = bvhManager.get();
        ctx.lightBvhManager = lightBvhManager.get();
        ctx.cameraController = cameraController.get();
        ctx.playModeActive = playModeActive;
        // Screen-space UI lays out and composites at DISPLAY resolution (getDisplayExtent), not the
        // render resolution (getSwapchainExtent, which is downscaled when upscaling/DLSS is active).
        // The UI pipelines normalize positions against getDisplayExtent(), so the layout space must
        // match — otherwise the HUD shrinks toward the origin when upscaling is on. The two extents
        // are equal when upscaling is off, so this is a no-op in that case.
        ctx.viewportWidth = swapChain.getDisplayExtent().width;
        ctx.viewportHeight = swapChain.getDisplayExtent().height;

        if (ctx.playModeActive)
        {
            auto& dispatcher = events::EventDispatcher::instance();
            glm::vec2 rawMouse = dispatcher.query(events::input::GetMousePositionQuery{}) - uiViewportOffset;

            // Scale mouse from viewport panel coordinates to framebuffer coordinates
            if (uiViewportPanelSize.x > 0.0f && uiViewportPanelSize.y > 0.0f)
            {
                float fbW = static_cast<float>(ctx.viewportWidth);
                float fbH = static_cast<float>(ctx.viewportHeight);
                rawMouse.x *= fbW / uiViewportPanelSize.x;
                rawMouse.y *= fbH / uiViewportPanelSize.y;
            }

            ctx.mousePosition = rawMouse;
            ctx.scrollDelta = dispatcher.query(events::input::GetScrollDeltaQuery{});
            events::input::IsMouseButtonDownQuery mouseQuery;
            mouseQuery.button = 0;
            ctx.leftMouseDown = dispatcher.query(mouseQuery);
            ctx.deltaTime = static_cast<float>(engineTime::Timer::getDeltaTime());

            // Edge detection for button press/release
            ctx.leftMousePressed = !prevLeftMouseDown && ctx.leftMouseDown;
            ctx.leftMouseReleased = prevLeftMouseDown && !ctx.leftMouseDown;
            prevLeftMouseDown = ctx.leftMouseDown;

            // Double-click detection
            events::input::IsDoubleClickQuery dblClickQuery;
            dblClickQuery.button = 0;
            ctx.leftMouseDoubleClick = dispatcher.query(dblClickQuery);

            // Keyboard/text input data for UITextInput interaction
            ctx.charInput = dispatcher.query(events::input::GetCharInputQuery{});
            ctx.isKeyPressed = [&dispatcher](int keyCode) {
                events::input::IsKeyPressedQuery q;
                q.keyCode = keyCode;
                return dispatcher.query(q);
            };
            ctx.isKeyDown = [&dispatcher](int keyCode) {
                events::input::IsKeyDownQuery q;
                q.keyCode = keyCode;
                return dispatcher.query(q);
            };
            ctx.getClipboardText = [&dispatcher]() {
                return dispatcher.query(events::input::GetClipboardTextQuery{});
            };
            ctx.setClipboardText = [&dispatcher](const std::string& text) {
                events::input::SetClipboardTextCommand cmd;
                cmd.text = text;
                dispatcher.execute(cmd);
            };
        }

        framePreparation->prepareUIImages(ctx);
    }
}
