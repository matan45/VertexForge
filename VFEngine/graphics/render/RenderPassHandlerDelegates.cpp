#include "RenderPassHandler.hpp"
#include "DebugRenderer.hpp"
#include "mesh/MeshTypes.hpp"
#include "occlusion/CameraOcclusionManager.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "gpudriven/terrain/TerrainRaycastPipeline.hpp"
#include "decal/DecalPipeline.hpp"

namespace render
{
    void RenderPassHandler::setGPUDrivenCameraData(const glm::vec3& cameraPos, float nearPlane, float farPlane,
                                                   float time)
    {
        currentCameraPosition = cameraPos;
        currentNearPlane = nearPlane;
        currentFarPlane = farPlane;
        currentTime = time;
    }

    void RenderPassHandler::setVisibleLightsFromBVH(const std::vector<uint32_t>& visibleLights)
    {
        if (gpuDrivenRenderer && gpuDrivenRendererInitialized)
        {
            gpuDrivenRenderer->setVisibleLightsFromBVH(visibleLights);
        }
    }

    void RenderPassHandler::clearVisibleLights()
    {
        if (gpuDrivenRenderer && gpuDrivenRendererInitialized)
        {
            gpuDrivenRenderer->clearVisibleLights();
        }
    }

    void RenderPassHandler::readBackLightOcclusionResults()
    {
        if (gpuDrivenRenderer && gpuDrivenRendererInitialized)
        {
            gpuDrivenRenderer->readBackLightOcclusionResults();

            // VSM feedback readback (1-frame latency, same sync point)
            auto* shadowSystem = gpuDrivenRenderer->getShadowSystem();
            if (shadowSystem && shadowSystem->isFeedbackEnabled())
            {
                shadowSystem->markFeedbackReady();
                shadowSystem->readBackFeedback();
            }
        }
    }

    // --- Terrain raycast ---

    void RenderPassHandler::readBackTerrainRaycastResults()
    {
        if (terrainRaycastPipeline && terrainRaycastPipeline->isInitialized())
        {
            terrainRaycastPipeline->readBackResults();
        }
    }

    void RenderPassHandler::setRaycastCursorUV(const glm::vec2& uv)
    {
        if (terrainRaycastPipeline)
        {
            terrainRaycastPipeline->setCursorUV(uv);
        }
    }

    void RenderPassHandler::clearRaycastCursor()
    {
        if (terrainRaycastPipeline)
        {
            terrainRaycastPipeline->clearCursor();
        }
    }

    terrain::TerrainHitResult RenderPassHandler::getTerrainHitResult() const
    {
        if (terrainRaycastPipeline && terrainRaycastPipeline->isInitialized())
        {
            return terrainRaycastPipeline->getLastResult();
        }
        return {};
    }

    void RenderPassHandler::setBrushOverlayParams(float radius, float falloff, float shape)
    {
        brushOverlayRadius_ = radius;
        brushOverlayFalloff_ = falloff;
        brushOverlayShape_ = shape;
    }

    void RenderPassHandler::updateBrushOverlayFromHitResult()
    {
        if (!gpuDrivenRendererInitialized || !gpuDrivenRenderer)
        {
            return;
        }

        auto hitResult = getTerrainHitResult();
        if (hitResult.hit && brushOverlayRadius_ > 0.0f)
        {
            glm::vec2 worldPos(hitResult.position.x, hitResult.position.z);
            gpuDrivenRenderer->setBrushOverlay(worldPos, brushOverlayRadius_, brushOverlayFalloff_, brushOverlayShape_);
        }
        else
        {
            gpuDrivenRenderer->setBrushOverlay(glm::vec2(0.0f), 0.0f, 0.0f, 0.0f);
        }
    }

    // --- GPU-driven view/culling setters ---

    void RenderPassHandler::setViewMode(uint32_t mode)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setViewMode(mode);
        }
    }

    uint32_t RenderPassHandler::getViewMode() const
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            return gpuDrivenRenderer->getViewMode();
        }
        return 0;
    }

    void RenderPassHandler::setFrustumCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setFrustumCullingEnabled(enabled);
        }
    }

    void RenderPassHandler::setOcclusionCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setOcclusionCullingEnabled(enabled);
        }
    }

    void RenderPassHandler::setLODSelectionEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setLODSelectionEnabled(enabled);
        }
    }

    void RenderPassHandler::setLODCrossfadeEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setLODCrossfadeEnabled(enabled);
        }
    }

    void RenderPassHandler::setMeshletFrustumCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setMeshletFrustumCullingEnabled(enabled);
        }
    }

    void RenderPassHandler::setMeshletBackfaceCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setMeshletBackfaceCullingEnabled(enabled);
        }
    }

    void RenderPassHandler::setMeshletOcclusionCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setMeshletOcclusionCullingEnabled(enabled);
        }
    }

    void RenderPassHandler::setGlobalLodBias(float bias)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setGlobalLodBias(bias);
        }
    }

    void RenderPassHandler::setTerrainFrustumCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainFrustumCullingEnabled(enabled);
        }
    }

    void RenderPassHandler::setTerrainMeshletCullingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainMeshletCullingEnabled(enabled);
        }
    }

    void RenderPassHandler::setWBOITEnabled(bool enabled)
    {
        wboitEnabled = enabled;
    }

    void RenderPassHandler::setTerrainRenderingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainRenderingEnabled(enabled);
        }
    }

    void RenderPassHandler::setBillboardRenderingEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setBillboardRenderingEnabled(enabled);
        }
    }

    void RenderPassHandler::setDecalRenderingEnabled(bool enabled)
    {
        decalRenderingEnabled = enabled;
    }

    void RenderPassHandler::setDecalDrawList(const std::vector<services::DecalRenderData>& decals)
    {
        if (decalPipeline)
        {
            decalPipeline->updateDecals(decals);
        }
    }

    void RenderPassHandler::setTerrainLODBias(float bias)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainLODBias(bias);
        }
    }

    void RenderPassHandler::setTerrainErrorThreshold(float threshold)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainErrorThreshold(threshold);
        }
    }

    void RenderPassHandler::setTerrainTextureScale(float scale)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainTextureScale(scale);
        }
    }

    void RenderPassHandler::setTerrainShadowLOD(uint32_t lod)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setTerrainShadowLOD(lod);
        }
    }

    void RenderPassHandler::setTerrainSVTEnabled(bool enabled)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            if (enabled && !gpuDrivenRenderer->isSVTEnabled())
            {
                gpuDrivenRenderer->initSVT();
            }
            gpuDrivenRenderer->setSVTEnabled(enabled);
        }
    }

    void RenderPassHandler::updateGPUDrivenHiZ() const
    {
        if (!gpuDrivenRendererInitialized || !gpuDrivenRenderer)
        {
            return;
        }

        occlusion::CameraId activeCameraId = cameraOcclusionManager->getActiveCameraId();
        if (!cameraOcclusionManager->isHiZInitialized(activeCameraId))
        {
            return;
        }

        auto* camera = cameraOcclusionManager->getCamera(activeCameraId);
        if (!camera || !camera->hiZBuffer || !camera->hiZBuffer->isInitialized())
        {
            return;
        }

        gpuDrivenRenderer->updateHiZPyramid(
            camera->hiZBuffer->getHiZImageView(),
            camera->hiZBuffer->getHiZSampler(),
            camera->hiZBuffer->getMipLevels()
        );

        if (!lightOcclusionInitialized)
        {
            gpuDrivenRenderer->initLightOcclusionCulling(camera->hiZBuffer.get());
            lightOcclusionInitialized = true;
        }
    }

    // --- Debug renderer delegation ---

    void RenderPassHandler::setCameraFrustumDrawList(std::vector<mesh::CameraFrustumRenderData>&& frustums)
    {
        if (debugRenderer)
        {
            debugRenderer->setCameraFrustumDrawList(std::move(frustums));
        }
    }

    void RenderPassHandler::setAudioSphereDrawList(std::vector<mesh::AudioSphereRenderData>&& spheres)
    {
        if (debugRenderer)
        {
            debugRenderer->setAudioSphereDrawList(std::move(spheres));
        }
    }

    void RenderPassHandler::setUICanvasOutlineDrawList(std::vector<mesh::UICanvasOutlineRenderData>&& outlines)
    {
        if (debugRenderer)
        {
            debugRenderer->setUICanvasOutlineDrawList(std::move(outlines));
        }
    }

    void RenderPassHandler::setUICanvasImageDrawList(std::vector<mesh::UICanvasImageRenderData>&& images)
    {
        if (debugRenderer)
        {
            debugRenderer->setUICanvasImageDrawList(std::move(images));
        }
    }

    void RenderPassHandler::setPhysicsColliderDrawList(std::vector<mesh::PhysicsColliderRenderData>&& colliders)
    {
        if (debugRenderer)
        {
            debugRenderer->setPhysicsColliderDrawList(std::move(colliders));
        }
    }

    void RenderPassHandler::setLightGizmoDrawList(std::vector<mesh::LightGizmoRenderData>&& gizmos)
    {
        if (debugRenderer)
        {
            debugRenderer->setLightGizmoDrawList(std::move(gizmos));
        }
    }

    void RenderPassHandler::setShowPhysicsDebug(bool show)
    {
        if (debugRenderer)
        {
            debugRenderer->setShowPhysicsDebug(show);
        }
    }

    bool RenderPassHandler::getShowPhysicsDebug() const
    {
        if (debugRenderer)
        {
            return debugRenderer->getShowPhysicsDebug();
        }
        return false;
    }

    void RenderPassHandler::setShowClusterDebug(bool show)
    {
        if (debugRenderer)
        {
            debugRenderer->setShowClusterDebug(show);
        }
    }

    bool RenderPassHandler::getShowClusterDebug() const
    {
        if (debugRenderer)
        {
            return debugRenderer->getShowClusterDebug();
        }
        return false;
    }

    void RenderPassHandler::setClusterDebugData(mesh::ClusterDebugRenderData&& data)
    {
        if (debugRenderer)
        {
            debugRenderer->setClusterDebugData(std::move(data));
        }
    }

    void RenderPassHandler::setShowNavmeshDebug(bool show)
    {
        if (debugRenderer)
        {
            debugRenderer->setShowNavmeshDebug(show);
        }
    }

    bool RenderPassHandler::getShowNavmeshDebug() const
    {
        if (debugRenderer)
        {
            return debugRenderer->getShowNavmeshDebug();
        }
        return false;
    }

    void RenderPassHandler::updateNavmeshDebugMesh(const std::vector<glm::vec3>& vertices,
                                                    const std::vector<uint32_t>& indices)
    {
        if (debugRenderer)
        {
            debugRenderer->updateNavmeshDebugMesh(vertices, indices);
        }
    }

    void RenderPassHandler::clearNavmeshDebugMesh()
    {
        if (debugRenderer)
        {
            debugRenderer->clearNavmeshDebugMesh();
        }
    }

    void RenderPassHandler::setDebugCameraMatrices(const glm::mat4& view, const glm::mat4& projection)
    {
        currentView = view;
        currentProjection = projection;
    }

    // --- Camera/occlusion forwarding ---

    occlusion::CameraRenderData* RenderPassHandler::createCamera(occlusion::CameraId id, bool enableOcclusion)
    {
        return cameraOcclusionManager->createCamera(id, enableOcclusion);
    }

    void RenderPassHandler::removeCamera(occlusion::CameraId id)
    {
        cameraOcclusionManager->removeCamera(id);
    }

    void RenderPassHandler::setActiveCamera(occlusion::CameraId id)
    {
        cameraOcclusionManager->setActiveCamera(id);
    }

    occlusion::CameraId RenderPassHandler::getActiveCameraId() const
    {
        return cameraOcclusionManager->getActiveCameraId();
    }

    void RenderPassHandler::initHiZ(occlusion::CameraId cameraId, vk::Image depthImage, vk::ImageView depthView,
                                    vk::Format depthFormat)
    {
        cameraOcclusionManager->initCameraHiZ(cameraId, depthImage, depthView, depthFormat);
    }

}
