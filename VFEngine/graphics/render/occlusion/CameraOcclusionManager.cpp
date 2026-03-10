#include "CameraOcclusionManager.hpp"
#include "../../core/Device.hpp"
#include "../../core/SwapChain.hpp"
#include "print/Log.hpp"

namespace render::occlusion
{
    CameraOcclusionManager::CameraOcclusionManager(core::Device& device, core::SwapChain& swapChain)
        : device(device), swapChain(swapChain)
    {
        createCamera(MAIN_CAMERA_ID, true);
    }

    CameraOcclusionManager::~CameraOcclusionManager()
    {
        cleanup();
    }

    CameraRenderData* CameraOcclusionManager::createCamera(CameraId id, bool enableOcclusion)
    {
        if (cameras.find(id) != cameras.end())
        {
            vfLogWarning("Camera {} already exists", id);
            return cameras[id].get();
        }

        auto cameraData = std::make_unique<CameraRenderData>(id);
        cameraData->useOcclusionCulling = enableOcclusion;

        CameraRenderData* ptr = cameraData.get();
        cameras[id] = std::move(cameraData);

        vfLogInfo("Created camera {} with occlusion culling {}", id, enableOcclusion ? "enabled" : "disabled");
        return ptr;
    }

    CameraRenderData* CameraOcclusionManager::getCamera(CameraId id)
    {
        auto it = cameras.find(id);
        return (it != cameras.end()) ? it->second.get() : nullptr;
    }

    void CameraOcclusionManager::removeCamera(CameraId id)
    {
        if (id == MAIN_CAMERA_ID)
        {
            vfLogWarning("Cannot remove main camera");
            return;
        }

        auto it = cameras.find(id);
        if (it != cameras.end())
        {
            cameras.erase(it);
            vfLogInfo("Removed camera {}", id);

            if (activeCameraId == id)
            {
                activeCameraId = MAIN_CAMERA_ID;
            }
        }
    }

    bool CameraOcclusionManager::hasCamera(CameraId id) const
    {
        return cameras.find(id) != cameras.end();
    }

    void CameraOcclusionManager::setActiveCamera(CameraId id)
    {
        if (hasCamera(id))
        {
            activeCameraId = id;
        }
        else
        {
            vfLogWarning("Cannot set active camera to non-existent camera {}", id);
        }
    }

    void CameraOcclusionManager::initCameraHiZ(CameraId id, vk::Image depthImage,
                                               vk::ImageView depthView, vk::Format depthFormat)
    {
        auto* camera = getCamera(id);
        if (!camera)
        {
            vfLogError("Cannot init Hi-Z for non-existent camera {}", id);
            return;
        }

        if (!camera->useOcclusionCulling)
        {
            vfLogInfo("Skipping Hi-Z init for camera {} (occlusion culling disabled)", id);
            return;
        }

        if (camera->hiZInitialized)
        {
            return;
        }

        camera->hiZBuffer = std::make_unique<HiZBuffer>(device, swapChain);
        camera->hiZBuffer->init(depthImage, depthView, depthFormat);
        camera->hiZInitialized = true;

        vfLogInfo("Initialized Hi-Z for camera {}", id);
    }

    void CameraOcclusionManager::recreateCameraHiZ(CameraId id, vk::Image depthImage,
                                                   vk::ImageView depthView, vk::Format depthFormat)
    {
        auto* camera = getCamera(id);
        if (!camera)
        {
            vfLogError("Cannot recreate Hi-Z for non-existent camera {}", id);
            return;
        }

        if (!camera->useOcclusionCulling)
        {
            return;
        }

        if (camera->hiZBuffer)
        {
            camera->hiZBuffer->cleanup();
        }
        camera->hiZInitialized = false;

        // Reinitialize Hi-Z with new depth buffer
        if (!camera->hiZBuffer)
        {
            camera->hiZBuffer = std::make_unique<HiZBuffer>(device, swapChain);
        }
        camera->hiZBuffer->init(depthImage, depthView, depthFormat);
        camera->hiZInitialized = true;
    }

    void CameraOcclusionManager::updateCamera(CameraId id, const glm::mat4& viewProj, float nearPlane)
    {
        auto* camera = getCamera(id);
        if (!camera) return;

        camera->viewProj = viewProj;
        camera->nearPlane = nearPlane;
    }

    void CameraOcclusionManager::updateCameraFrustum(CameraId id, const math::Frustum& frustum)
    {
        auto* camera = getCamera(id);
        if (!camera) return;

        camera->frustum = frustum;
    }

    void CameraOcclusionManager::generateHiZ(CameraId id, vk::CommandBuffer cmd)
    {
        auto* camera = getCamera(id);
        if (!camera || !camera->hiZBuffer || !camera->hiZInitialized) return;

        camera->hiZBuffer->generate(cmd);
    }

    void CameraOcclusionManager::cleanup()
    {
        for (auto& [id, camera] : cameras)
        {
            if (camera->hiZBuffer)
            {
                camera->hiZBuffer->cleanup();
            }
        }
        cameras.clear();
    }

    bool CameraOcclusionManager::isHiZInitialized(CameraId id)
    {
        auto* camera = getCamera(id);
        return camera && camera->hiZInitialized;
    }
}
