#include "RenderPassHandler.hpp"
#include "DebugRenderer.hpp"
#include "mesh/MeshTypes.hpp"
#include "occlusion/CameraOcclusionManager.hpp"
#include "gpudriven/GPUDrivenRenderer.hpp"
#include "gpudriven/terrain/TerrainRaycastPipeline.hpp"
#include "decal/DecalPipeline.hpp"
#include "atmosphere/AtmospherePipeline.hpp"
#include "atmosphere/SunTransmittance.hpp"
#include "stats/TerrainRVTStats.hpp" // VK-1610: clear the residency readout when RVT is inactive
#include "print/Log.hpp"
#include <optional>
#include <algorithm>

namespace render
{
    void RenderPassHandler::setGPUDrivenCameraData(const glm::vec3& cameraPos, float nearPlane, float farPlane,
                                                   float time)
    {
        currentCameraPosition = cameraPos;
        currentNearPlane = nearPlane;
        currentFarPlane = farPlane;
        currentTime = time;

        // VK-1566: tint the sun (index-0 directional light) by the atmospheric transmittance.
        // Runs once per frame, upstream of both the sync and async-compute paths. The day-night
        // cycle is advanced earlier in the frame by the Services SunSync step (before the
        // transform bake), so settings.sunAzimuth/sunElevation are already fresh here.
        glm::vec3 sunTint(1.0f);
        if (atmospherePipeline && atmospherePipeline->isEnabled())
        {
            const atmosphere::AtmosphereSettings s = atmospherePipeline->getSettings();
            if (s.sunColorFromAtmosphere)
            {
                glm::vec3 dirToSun;
                if (s.dayNightEnabled)
                {
                    // Cycle on: sun direction comes from the freshly-advanced angles.
                    dirToSun = atmosphere::sunDirectionFromAngles(s.sunAzimuth, s.sunElevation);
                }
                else
                {
                    // Cycle off: derive from the first directional light (negated: light-travel -> to-sun).
                    std::optional<glm::vec3> firstDir;
                    if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
                        firstDir = gpuDrivenRenderer->getLightBufferManager()->getFirstDirectionalLightDirection();
                    dirToSun = firstDir ? -*firstDir
                                        : atmosphere::sunDirectionFromAngles(s.sunAzimuth, s.sunElevation);
                }

                const glm::vec3 transmittance =
                    atmosphere::evaluateSunTransmittance(s, dirToSun, std::max(0.0f, currentCameraPosition.y));
                sunTint = glm::mix(glm::vec3(1.0f), transmittance,
                                   glm::clamp(s.sunColorFeedbackStrength, 0.0f, 1.0f));
            }
        }
        // Always push (resets to vec3(1) when the feature/atmosphere is off, so no stale tint persists).
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
            gpuDrivenRenderer->getLightBufferManager()->setSunColorMultiplier(sunTint);
    }

    void RenderPassHandler::updateSharedCameraUBO(const glm::mat4& view, const glm::mat4& projection,
                                                   const glm::vec3& cameraPos, float time)
    {
        if (sharedCameraUBO)
        {
            sharedCameraUBO->update(view, projection, cameraPos, time, currentSnowAccumulation, currentWetness,
                                    currentIblTintIntensity, currentIblRotation);
        }
    }

    vk::Buffer RenderPassHandler::getSharedCameraBuffer() const
    {
        return sharedCameraUBO ? sharedCameraUBO->getBuffer() : nullptr;
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

            // VK-1209: terrain RVT feedback readback + residency planning (same fence-gated point).
            if (gpuDrivenRenderer->isTerrainRVTActive())
                gpuDrivenRenderer->updateTerrainRVTResidency();
            else
                // VK-1610: RVT off, or the manager was torn down by a plane-layout change. The
                // residency readout is published from inside updateTerrainRVTResidency, so without
                // this the profiler would keep showing the last live frame as if it were current.
                render::TerrainRVTStats::instance().clear();

            // VK-1209: material SVT feedback readback (same point).
            if (gpuDrivenRenderer->isSVTActive())
                gpuDrivenRenderer->beginSVTFrame();
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

    void RenderPassHandler::setBrushOverlayParams(float radius, float falloff, float shape, float stampRotation)
    {
        brushOverlayRadius_ = radius;
        brushOverlayFalloff_ = falloff;
        brushOverlayShape_ = shape;
        brushOverlayStampRotation_ = stampRotation;
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
            gpuDrivenRenderer->setBrushOverlay(hitResult.position, brushOverlayRadius_, brushOverlayFalloff_, brushOverlayShape_, brushOverlayStampRotation_);
        }
        else
        {
            gpuDrivenRenderer->setBrushOverlay(glm::vec3(0.0f), 0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    void RenderPassHandler::setStampOverlay(vk::Buffer buffer, uint32_t width, uint32_t height, float rotation)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setStampOverlay(buffer, width, height, rotation);
        }
    }

    void RenderPassHandler::clearStampOverlay()
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->clearStampOverlay();
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

    void RenderPassHandler::updateBillboards(
        std::vector<render::gpudriven::BillboardInstanceGPU> instances,
        const std::vector<std::string>& texturePaths)
    {
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->updateBillboards(std::move(instances), texturePaths);
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

    void RenderPassHandler::updateGPUDrivenHiZ() const
    {
        if (!gpuDrivenRendererInitialized || !gpuDrivenRenderer)
        {
            return;
        }

        // VK-1336: main-scene HiZ pyramid always belongs to the primary camera.
        // RTT cameras run their HiZ inside RenderTextureViewPort's per-RTT context.
        if (!cameraOcclusionManager->isHiZInitialized(occlusion::MAIN_CAMERA_ID))
        {
            return;
        }

        auto* camera = cameraOcclusionManager->getCamera(occlusion::MAIN_CAMERA_ID);
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

    void RenderPassHandler::setSelectedEntityDrawList(std::vector<uint32_t>&& entityIds)
    {
        // Pushed every editor frame; skip the downstream unordered_set rebuild
        // whenever the selection is unchanged from the last push (empty stays the
        // common fast path). Order-sensitive compare only ever errs toward doing a
        // redundant rebuild, never toward missing a real change.
        if (entityIds == selectedEntityIds)
        {
            return;
        }

        selectedEntityIds = std::move(entityIds);
        if (gpuDrivenRendererInitialized && gpuDrivenRenderer)
        {
            gpuDrivenRenderer->setSelectedEntities(std::unordered_set<uint32_t>(
                selectedEntityIds.begin(), selectedEntityIds.end()));
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

    void RenderPassHandler::setWireframeMode(bool enabled)
    {
        if (gpuDrivenRenderer && gpuDrivenRendererInitialized)
        {
            gpuDrivenRenderer->setWireframeMode(enabled);
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
        prevPrevView = prevView;
        prevPrevProjection = prevProjection;
        prevView = currentView;
        prevProjection = currentProjection;
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
