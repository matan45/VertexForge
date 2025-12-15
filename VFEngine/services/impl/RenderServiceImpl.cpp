#include "RenderServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"

namespace services {

    RenderServiceImpl::RenderServiceImpl(IOffScreenProvider* offScreenProvider,
                                         IEditorTextureProvider* textureProvider)
        : offScreenProvider(offScreenProvider)
        , textureProvider(textureProvider) {}

    RenderServiceImpl::~RenderServiceImpl() = default;

    ViewportTextureHandle RenderServiceImpl::getViewportTexture() {
        if (!offScreenProvider) {
            return ViewportTextureHandle{};
        }

        frameCounter++;

        // Prepare mesh draw list from ECS entities before rendering
        prepareFrameMeshes();

        void* descriptorSet = offScreenProvider->render();

        ViewportTextureHandle handle;
        handle.imguiDescriptorSet = descriptorSet;
        handle.width = viewportWidth;
        handle.height = viewportHeight;

        return handle;
    }

    void RenderServiceImpl::resizeViewport(uint32_t width, uint32_t height) {
        if (width == viewportWidth && height == viewportHeight) {
            return;
        }

        viewportWidth = width;
        viewportHeight = height;

        // Publish resize notification
        events::render::ViewportResizedNotification notification;
        notification.width = width;
        notification.height = height;
        events::EventDispatcher::instance().publish(notification);
    }

    void RenderServiceImpl::getViewportSize(uint32_t& width, uint32_t& height) const {
        width = viewportWidth;
        height = viewportHeight;
    }

    bool RenderServiceImpl::setIBL(const std::string& hdrPath) {
        if (!offScreenProvider) {
            return false;
        }

        // Remove existing IBL before adding new one
        if (currentIBLPath.has_value()) {
            offScreenProvider->iblRemove();
        }

        // Initialize IBL - camera matrices will be set separately via updateIBLCamera
        offScreenProvider->iblSet(hdrPath);
        currentIBLPath = hdrPath;

        // Publish IBL changed notification
        events::render::IBLChangedNotification notification;
        notification.hdrPath = hdrPath;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    void RenderServiceImpl::updateIBLCamera(const glm::mat4& view, const glm::mat4& projection) {
        if (!offScreenProvider || !currentIBLPath.has_value()) {
            return;
        }

        offScreenProvider->iblSetCameraMatrices(view, projection);
    }

    void RenderServiceImpl::removeIBL() {
        if (!offScreenProvider) {
            return;
        }

        offScreenProvider->iblRemove();
        currentIBLPath = std::nullopt;

        // Publish IBL changed notification
        events::render::IBLChangedNotification notification;
        notification.hdrPath = std::nullopt;
        events::EventDispatcher::instance().publish(notification);
    }

    bool RenderServiceImpl::hasIBL() const {
        return currentIBLPath.has_value();
    }

    std::optional<std::string> RenderServiceImpl::getIBLPath() const {
        return currentIBLPath;
    }

    EditorTextureHandle RenderServiceImpl::loadEditorTexture(const std::string& path) {
        if (!textureProvider) {
            return EditorTextureHandle{};
        }

        auto textureData = textureProvider->loadTexture(path);

        if (!textureData.valid) {
            return EditorTextureHandle{};
        }

        EditorTextureHandle handle;
        handle.imguiDescriptorSet = textureData.descriptorSet;
        handle.width = static_cast<uint32_t>(textureData.width);
        handle.height = static_cast<uint32_t>(textureData.height);

        // Track for cleanup
        loadedTextures[handle.imguiDescriptorSet] = handle;

        return handle;
    }

    EditorTextureHandle RenderServiceImpl::loadEditorHDRTexture(const std::string& path) {
        if (!textureProvider) {
            return EditorTextureHandle{};
        }

        auto textureData = textureProvider->loadHdrTexture(path);

        if (!textureData.valid) {
            return EditorTextureHandle{};
        }

        EditorTextureHandle handle;
        handle.imguiDescriptorSet = textureData.descriptorSet;
        handle.width = static_cast<uint32_t>(textureData.width);
        handle.height = static_cast<uint32_t>(textureData.height);

        // Track for cleanup
        loadedTextures[handle.imguiDescriptorSet] = handle;

        return handle;
    }

    void RenderServiceImpl::releaseEditorTexture(const EditorTextureHandle& handle) {
        if (textureProvider && loadedTextures.count(handle.imguiDescriptorSet)) {
            textureProvider->releaseTexture(handle.imguiDescriptorSet);
            loadedTextures.erase(handle.imguiDescriptorSet);
        }
    }

    bool RenderServiceImpl::isReady() const {
        return offScreenProvider != nullptr;
    }

    uint64_t RenderServiceImpl::getFrameNumber() const {
        return frameCounter;
    }

    void RenderServiceImpl::registerEventHandlers() {
        auto& dispatcher = events::EventDispatcher::instance();

        // Command handlers
        dispatcher.registerCommandHandler<events::render::SetIBLCommand>(
            [this](const events::render::SetIBLCommand& cmd) {
                return setIBL(cmd.hdrPath);
            });

        dispatcher.registerCommandHandler<events::render::RemoveIBLCommand>(
            [this](const events::render::RemoveIBLCommand&) {
                removeIBL();
            });

        dispatcher.registerCommandHandler<events::render::UpdateIBLCameraCommand>(
            [this](const events::render::UpdateIBLCameraCommand& cmd) {
                updateIBLCamera(cmd.viewMatrix, cmd.projectionMatrix);
            });

        dispatcher.registerCommandHandler<events::render::ResizeViewportCommand>(
            [this](const events::render::ResizeViewportCommand& cmd) {
                resizeViewport(cmd.width, cmd.height);
            });

        dispatcher.registerCommandHandler<events::render::LoadEditorTextureCommand>(
            [this](const events::render::LoadEditorTextureCommand& cmd) {
                if (cmd.isHDR) {
                    return loadEditorHDRTexture(cmd.path);
                }
                return loadEditorTexture(cmd.path);
            });

        dispatcher.registerCommandHandler<events::render::ReleaseEditorTextureCommand>(
            [this](const events::render::ReleaseEditorTextureCommand& cmd) {
                EditorTextureHandle handle;
                handle.imguiDescriptorSet = cmd.handle;
                releaseEditorTexture(handle);
            });

        // Query handlers
        dispatcher.registerQueryHandler<events::render::GetViewportTextureQuery>(
            [this](const events::render::GetViewportTextureQuery&) {
                return getViewportTexture();
            });

        dispatcher.registerQueryHandler<events::render::HasIBLQuery>(
            [this](const events::render::HasIBLQuery&) {
                return hasIBL();
            });

        dispatcher.registerQueryHandler<events::render::GetIBLPathQuery>(
            [this](const events::render::GetIBLPathQuery&) {
                return getIBLPath();
            });

        // Mesh command handlers
        dispatcher.registerCommandHandler<events::render::LoadMeshCommand>(
            [this](const events::render::LoadMeshCommand& cmd) {
                return loadMesh(cmd.meshPath);
            });

        dispatcher.registerCommandHandler<events::render::UnloadMeshCommand>(
            [this](const events::render::UnloadMeshCommand& cmd) {
                unloadMesh(cmd.meshId);
            });

        dispatcher.registerCommandHandler<events::render::UpdateMeshCameraCommand>(
            [this](const events::render::UpdateMeshCameraCommand& cmd) {
                updateMeshCamera(cmd.viewMatrix, cmd.projectionMatrix, cmd.cameraPosition, cmd.time);
            });

        // Mesh query handlers
        dispatcher.registerQueryHandler<events::render::IsMeshLoadedQuery>(
            [this](const events::render::IsMeshLoadedQuery& q) {
                return isMeshLoaded(q.meshPath);
            });

        dispatcher.registerQueryHandler<events::render::GetLoadedMeshesQuery>(
            [this](const events::render::GetLoadedMeshesQuery&) {
                return getLoadedMeshes();
            });

        // Subscribe to mesh data changes to preload meshes when they're assigned to entities
        // This avoids synchronous loading during frame preparation which causes frame spikes
        meshDataChangedToken = dispatcher.subscribe<events::scene::MeshDataChangedNotification>(
            [this](const events::scene::MeshDataChangedNotification& notification) {
                if (!notification.meshPath.empty() && !isMeshLoaded(notification.meshPath)) {
                    loadMesh(notification.meshPath);
                }
            });
    }

    // Mesh Operations
    std::string RenderServiceImpl::loadMesh(const std::string& meshPath) {
        if (!offScreenProvider) {
            return "";
        }
        return offScreenProvider->meshLoad(meshPath);
    }

    void RenderServiceImpl::unloadMesh(const std::string& meshId) {
        if (offScreenProvider) {
            offScreenProvider->meshUnload(meshId);
        }
    }

    void RenderServiceImpl::updateMeshCamera(const glm::mat4& view, const glm::mat4& projection,
                                             const glm::vec3& cameraPos, float time) {
        if (offScreenProvider) {
            offScreenProvider->meshUpdateCamera(view, projection, cameraPos, time);
        }
    }

    bool RenderServiceImpl::isMeshLoaded(const std::string& meshPath) const {
        return offScreenProvider && offScreenProvider->isMeshLoaded(meshPath);
    }

    std::vector<std::string> RenderServiceImpl::getLoadedMeshes() const {
        if (!offScreenProvider) {
            return {};
        }
        return offScreenProvider->getLoadedMeshes();
    }

    void RenderServiceImpl::prepareFrameMeshes() {
        if (offScreenProvider) {
            offScreenProvider->prepareFrameMeshes();
        }
    }

}
