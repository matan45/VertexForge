#include "EditorRenderServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "print/EditorLogger.hpp"
#include <filesystem>

namespace services
{
    EditorRenderServiceImpl::EditorRenderServiceImpl(IOffScreenProvider* offScreenProvider,
                                                     IEditorTextureProvider* textureProvider)
        : offScreenProvider(offScreenProvider)
          , textureProvider(textureProvider)
    {
    }

    ViewportTextureHandle EditorRenderServiceImpl::getViewportTexture()
    {
        if (!offScreenProvider)
        {
            return ViewportTextureHandle{};
        }

        frameCounter++;
        prepareFrameMeshes();

        void* descriptorSet = offScreenProvider->render();

        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = descriptorSet;
        handle.width = viewportWidth;
        handle.height = viewportHeight;

        return handle;
    }

    void EditorRenderServiceImpl::resizeViewport(uint32_t width, uint32_t height)
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

    void EditorRenderServiceImpl::getViewportSize(uint32_t& width, uint32_t& height) const
    {
        width = viewportWidth;
        height = viewportHeight;
    }

    bool EditorRenderServiceImpl::setIBL(const std::string& hdrPath)
    {
        if (!offScreenProvider)
        {
            return false;
        }
        
        if (!std::filesystem::exists(hdrPath))
        {
            vfLogError("IBL file not found: {}", hdrPath);
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

    void EditorRenderServiceImpl::updateIBLCamera(const glm::mat4& view, const glm::mat4& projection)
    {
        if (!offScreenProvider || !currentIBLPath.has_value())
        {
            return;
        }
        offScreenProvider->iblSetCameraMatrices(view, projection);
    }

    void EditorRenderServiceImpl::removeIBL()
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

    bool EditorRenderServiceImpl::hasIBL() const
    {
        return currentIBLPath.has_value();
    }

    std::optional<std::string> EditorRenderServiceImpl::getIBLPath() const
    {
        return currentIBLPath;
    }

    EditorTextureHandle EditorRenderServiceImpl::loadEditorTexture(const std::string& path)
    {
        if (!textureProvider)
        {
            return EditorTextureHandle{};
        }

        auto textureData = textureProvider->loadTexture(path);

        if (!textureData.valid)
        {
            return EditorTextureHandle{};
        }

        EditorTextureHandle handle;
        handle.imguiDescriptorSet = textureData.descriptorSet;
        handle.width = static_cast<uint32_t>(textureData.width);
        handle.height = static_cast<uint32_t>(textureData.height);
        handle.mipLevels = static_cast<uint32_t>(textureData.mipLevels);
        handle.mipDescriptorSets = textureData.mipDescriptorSets;

        loadedTextures[handle.imguiDescriptorSet] = handle;

        return handle;
    }

    EditorTextureHandle EditorRenderServiceImpl::loadEditorHDRTexture(const std::string& path)
    {
        if (!textureProvider)
        {
            return EditorTextureHandle{};
        }

        auto textureData = textureProvider->loadHdrTexture(path);

        if (!textureData.valid)
        {
            return EditorTextureHandle{};
        }

        EditorTextureHandle handle;
        handle.imguiDescriptorSet = textureData.descriptorSet;
        handle.width = static_cast<uint32_t>(textureData.width);
        handle.height = static_cast<uint32_t>(textureData.height);
        handle.mipLevels = static_cast<uint32_t>(textureData.mipLevels);
        handle.mipDescriptorSets = textureData.mipDescriptorSets;

        loadedTextures[handle.imguiDescriptorSet] = handle;

        return handle;
    }

    void EditorRenderServiceImpl::releaseEditorTexture(const EditorTextureHandle& handle)
    {
        if (textureProvider && loadedTextures.count(handle.imguiDescriptorSet))
        {
            textureProvider->releaseTexture(handle.imguiDescriptorSet);
            loadedTextures.erase(handle.imguiDescriptorSet);
        }
    }

    bool EditorRenderServiceImpl::isReady() const
    {
        return offScreenProvider != nullptr;
    }

    uint64_t EditorRenderServiceImpl::getFrameNumber() const
    {
        return frameCounter;
    }

    void EditorRenderServiceImpl::registerEventHandlers()
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

        dispatcher.registerCommandHandler<events::render::LoadEditorTextureCommand>(
            [this](const events::render::LoadEditorTextureCommand& cmd)
            {
                if (cmd.isHDR)
                {
                    return loadEditorHDRTexture(cmd.path);
                }
                return loadEditorTexture(cmd.path);
            });

        dispatcher.registerCommandHandler<events::render::ReleaseEditorTextureCommand>(
            [this](const events::render::ReleaseEditorTextureCommand& cmd)
            {
                EditorTextureHandle handle;
                handle.imguiDescriptorSet = cmd.handle;
                releaseEditorTexture(handle);
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

        meshDataChangedToken = dispatcher.subscribe<events::scene::MeshDataChangedNotification>(
            [this](const events::scene::MeshDataChangedNotification& notification)
            {
                if (!notification.meshPath.empty() && !isMeshLoaded(notification.meshPath))
                {
                    loadMesh(notification.meshPath);
                }
            });
    }

    std::string EditorRenderServiceImpl::loadMesh(const std::string& meshPath)
    {
        if (!offScreenProvider)
        {
            return "";
        }
        return offScreenProvider->meshLoad(meshPath);
    }

    void EditorRenderServiceImpl::unloadMesh(const std::string& meshId)
    {
        if (offScreenProvider)
        {
            offScreenProvider->meshUnload(meshId);
        }
    }

    void EditorRenderServiceImpl::updateMeshCamera(const glm::mat4& view, const glm::mat4& projection,
                                                   const glm::vec3& cameraPos, float time)
    {
        if (offScreenProvider)
        {
            offScreenProvider->meshUpdateCamera(view, projection, cameraPos, time);
        }
    }

    bool EditorRenderServiceImpl::isMeshLoaded(const std::string& meshPath) const
    {
        return offScreenProvider && offScreenProvider->isMeshLoaded(meshPath);
    }

    std::vector<std::string> EditorRenderServiceImpl::getLoadedMeshes() const
    {
        if (!offScreenProvider)
        {
            return {};
        }
        return offScreenProvider->getLoadedMeshes();
    }

    void EditorRenderServiceImpl::prepareFrameMeshes()
    {
        if (offScreenProvider)
        {
            offScreenProvider->prepareFrameMeshes();
        }
    }
}
