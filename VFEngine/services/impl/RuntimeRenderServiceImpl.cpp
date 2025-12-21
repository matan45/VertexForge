#include "RuntimeRenderServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "print/Logger.hpp"
#include <filesystem>

namespace services
{
    RuntimeRenderServiceImpl::RuntimeRenderServiceImpl(IOffScreenProvider* offScreenProvider)
        : offScreenProvider(offScreenProvider)
    {
    }

    ViewportTextureHandle RuntimeRenderServiceImpl::getViewportTexture()
    {
        if (!offScreenProvider)
        {
            return ViewportTextureHandle{};
        }

        frameCounter++;
        prepareCameras();
        prepareFrameMeshes();

        void* descriptorSet = offScreenProvider->render();

        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = descriptorSet;
        handle.width = viewportWidth;
        handle.height = viewportHeight;

        return handle;
    }

    void RuntimeRenderServiceImpl::resizeViewport(uint32_t width, uint32_t height)
    {
        if (width == viewportWidth && height == viewportHeight)
        {
            return;
        }

        viewportWidth = width;
        viewportHeight = height;

        events::render::ViewportResizedNotification notification;
        notification.width = width;
        notification.height = height;
        events::EventDispatcher::instance().publish(notification);
    }

    void RuntimeRenderServiceImpl::getViewportSize(uint32_t& width, uint32_t& height) const
    {
        width = viewportWidth;
        height = viewportHeight;
    }

    bool RuntimeRenderServiceImpl::setIBL(const std::string& hdrPath)
    {
        if (!offScreenProvider)
        {
            return false;
        }
        
        if (!std::filesystem::exists(hdrPath))
        {
            loggerError("IBL file not found: {}", hdrPath);
            return false;
        }

        if (currentIBLPath.has_value())
        {
            offScreenProvider->iblRemove();
        }

        offScreenProvider->iblSet(hdrPath);
        currentIBLPath = hdrPath;

        events::render::IBLChangedNotification notification;
        notification.hdrPath = hdrPath;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    void RuntimeRenderServiceImpl::updateIBLCamera(const glm::mat4& view, const glm::mat4& projection)
    {
        if (!offScreenProvider || !currentIBLPath.has_value())
        {
            return;
        }
        offScreenProvider->iblSetCameraMatrices(view, projection);
    }

    void RuntimeRenderServiceImpl::removeIBL()
    {
        if (!offScreenProvider)
        {
            return;
        }

        offScreenProvider->iblRemove();
        currentIBLPath = std::nullopt;

        events::render::IBLChangedNotification notification;
        notification.hdrPath = std::nullopt;
        events::EventDispatcher::instance().publish(notification);
    }

    bool RuntimeRenderServiceImpl::hasIBL() const
    {
        return currentIBLPath.has_value();
    }

    std::optional<std::string> RuntimeRenderServiceImpl::getIBLPath() const
    {
        return currentIBLPath;
    }

    bool RuntimeRenderServiceImpl::isReady() const
    {
        return offScreenProvider != nullptr;
    }

    uint64_t RuntimeRenderServiceImpl::getFrameNumber() const
    {
        return frameCounter;
    }

    void RuntimeRenderServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::render::SetIBLCommand>(
            [this](const events::render::SetIBLCommand& cmd)
            {
                return setIBL(cmd.hdrPath);
            });

        dispatcher.registerCommandHandler<events::render::RemoveIBLCommand>(
            [this](const events::render::RemoveIBLCommand&)
            {
                removeIBL();
            });

        dispatcher.registerCommandHandler<events::render::UpdateIBLCameraCommand>(
            [this](const events::render::UpdateIBLCameraCommand& cmd)
            {
                updateIBLCamera(cmd.viewMatrix, cmd.projectionMatrix);
            });

        dispatcher.registerCommandHandler<events::render::ResizeViewportCommand>(
            [this](const events::render::ResizeViewportCommand& cmd)
            {
                resizeViewport(cmd.width, cmd.height);
            });

        dispatcher.registerQueryHandler<events::render::GetViewportTextureQuery>(
            [this](const events::render::GetViewportTextureQuery&)
            {
                return getViewportTexture();
            });

        dispatcher.registerQueryHandler<events::render::HasIBLQuery>(
            [this](const events::render::HasIBLQuery&)
            {
                return hasIBL();
            });

        dispatcher.registerQueryHandler<events::render::GetIBLPathQuery>(
            [this](const events::render::GetIBLPathQuery&)
            {
                return getIBLPath();
            });

        dispatcher.registerCommandHandler<events::render::LoadMeshCommand>(
            [this](const events::render::LoadMeshCommand& cmd)
            {
                return loadMesh(cmd.meshPath);
            });

        dispatcher.registerCommandHandler<events::render::UnloadMeshCommand>(
            [this](const events::render::UnloadMeshCommand& cmd)
            {
                unloadMesh(cmd.meshId);
            });

        dispatcher.registerCommandHandler<events::render::UpdateMeshCameraCommand>(
            [this](const events::render::UpdateMeshCameraCommand& cmd)
            {
                updateMeshCamera(cmd.viewMatrix, cmd.projectionMatrix, cmd.cameraPosition, cmd.time);
            });

        dispatcher.registerQueryHandler<events::render::IsMeshLoadedQuery>(
            [this](const events::render::IsMeshLoadedQuery& q)
            {
                return isMeshLoaded(q.meshPath);
            });

        dispatcher.registerQueryHandler<events::render::GetLoadedMeshesQuery>(
            [this](const events::render::GetLoadedMeshesQuery&)
            {
                return getLoadedMeshes();
            });

        dispatcher.registerQueryHandler<events::render::GetMeshBoundingBoxQuery>(
            [this](const events::render::GetMeshBoundingBoxQuery& q)
            {
                return getMeshBoundingBox(q.meshPath);
            });

        meshDataChangedToken = dispatcher.subscribe<events::scene::MeshDataChangedNotification>(
            [this](const events::scene::MeshDataChangedNotification& notification)
            {
                if (!notification.meshPath.empty() && !isMeshLoaded(notification.meshPath))
                {
                    loadMesh(notification.meshPath);
                }
            });
    }

    std::string RuntimeRenderServiceImpl::loadMesh(const std::string& meshPath)
    {
        if (!offScreenProvider)
        {
            return "";
        }
        return offScreenProvider->meshLoad(meshPath);
    }

    void RuntimeRenderServiceImpl::unloadMesh(const std::string& meshId)
    {
        if (offScreenProvider)
        {
            offScreenProvider->meshUnload(meshId);
        }
    }

    void RuntimeRenderServiceImpl::updateMeshCamera(const glm::mat4& view, const glm::mat4& projection,
                                                    const glm::vec3& cameraPos, float time)
    {
        if (offScreenProvider)
        {
            offScreenProvider->meshUpdateCamera(MAIN_CAMERA_ID, view, projection, cameraPos, time);
        }
    }

    bool RuntimeRenderServiceImpl::isMeshLoaded(const std::string& meshPath) const
    {
        return offScreenProvider && offScreenProvider->isMeshLoaded(meshPath);
    }

    std::vector<std::string> RuntimeRenderServiceImpl::getLoadedMeshes() const
    {
        if (!offScreenProvider)
        {
            return {};
        }
        return offScreenProvider->getLoadedMeshes();
    }

    std::optional<MeshBoundingBox> RuntimeRenderServiceImpl::getMeshBoundingBox(const std::string& meshPath) const
    {
        if (!offScreenProvider)
        {
            return std::nullopt;
        }
        auto bounds = offScreenProvider->getMeshBoundingBox(meshPath);
        if (!bounds)
        {
            return std::nullopt;
        }
        MeshBoundingBox result;
        result.min = bounds->min;
        result.max = bounds->max;
        return result;
    }

    void RuntimeRenderServiceImpl::prepareCameras()
    {
        if (offScreenProvider)
        {
            offScreenProvider->prepareCameras();
        }
    }

    void RuntimeRenderServiceImpl::prepareFrameMeshes()
    {
        if (offScreenProvider)
        {
            offScreenProvider->prepareFrameMeshes();
        }
    }
}
