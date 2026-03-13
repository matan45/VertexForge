#include "OffScreenController.hpp"
#include "../render/OffScreenViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../render/DebugRenderer.hpp"
#include "../render/gpudriven/brush/BrushComputePipeline.hpp"

namespace controllers
{
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

    void OffScreenController::setShowNavmeshDebug(bool show)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            if (show && renderHandler->isMeshPipelineInitialized() && !renderHandler->isDebugRendererInitialized())
            {
                renderHandler->initDebugRenderer();
            }

            if (renderHandler->isDebugRendererInitialized())
            {
                renderHandler->setShowNavmeshDebug(show);
            }
        }
    }

    bool OffScreenController::getShowNavmeshDebug() const
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler && renderHandler->isDebugRendererInitialized())
        {
            return renderHandler->getShowNavmeshDebug();
        }
        return false;
    }

    void OffScreenController::updateNavmeshDebugMesh(const std::vector<glm::vec3>& vertices,
                                                      const std::vector<uint32_t>& indices)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            if (renderHandler->isMeshPipelineInitialized() && !renderHandler->isDebugRendererInitialized())
            {
                renderHandler->initDebugRenderer();
            }

            if (renderHandler->isDebugRendererInitialized())
            {
                renderHandler->updateNavmeshDebugMesh(vertices, indices);
            }
        }
    }

    void OffScreenController::clearNavmeshDebugMesh()
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler && renderHandler->isDebugRendererInitialized())
        {
            renderHandler->clearNavmeshDebugMesh();
        }
    }

    void OffScreenController::updateImmediateDebugDrawList(render::mesh::ImmediateDebugDrawList drawList)
    {
        auto* renderHandler = offScreen->getRenderPassHandler();
        if (renderHandler)
        {
            if (renderHandler->isMeshPipelineInitialized() && !renderHandler->isDebugRendererInitialized())
            {
                renderHandler->initDebugRenderer();
            }

            if (renderHandler->isDebugRendererInitialized())
            {
                auto* debugRendererPtr = renderHandler->getDebugRenderer();
                if (debugRendererPtr)
                {
                    debugRendererPtr->updateImmediateDebugDrawList(std::move(drawList));
                }
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
