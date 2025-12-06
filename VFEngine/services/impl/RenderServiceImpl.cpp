#include "RenderServiceImpl.hpp"
#include "../../core/controllers/OffScreen.hpp"
#include "../../core/controllers/EditorTextureController.hpp"
#include "../../core/controllers/texture/EditorTexture.hpp"
#include "SceneServiceImpl.hpp"
#include "../../utilities/scene/EntityRegistry.hpp"
#include "../../utilities/components/Components.hpp"

namespace services {

    RenderServiceImpl::RenderServiceImpl(controllers::OffScreen* offScreen)
        : offScreen(offScreen) {}

    RenderServiceImpl::~RenderServiceImpl() = default;

    ViewportTextureHandle RenderServiceImpl::getViewportTexture() {
        if (!offScreen) {
            return ViewportTextureHandle{};
        }

        frameCounter++;

        void* descriptorSet = offScreen->render();

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
        if (!offScreen) {
            return false;
        }

        // Find the first camera entity to pass to IBL
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::CameraComponent>();

        components::CameraComponent* camera = nullptr;
        for (auto entity : view) {
            camera = &registry.get<components::CameraComponent>(entity);
            break;
        }

        if (!camera) {
            return false;
        }

        // Remove existing IBL before adding new one
        if (currentIBLPath.has_value()) {
            offScreen->iblRemove();
        }

        offScreen->iblAdd(hdrPath, camera);
        currentIBLPath = hdrPath;

        // Publish IBL changed notification
        events::render::IBLChangedNotification notification;
        notification.hdrPath = hdrPath;
        events::EventDispatcher::instance().publish(notification);

        return true;
    }

    void RenderServiceImpl::removeIBL() {
        if (!offScreen) {
            return;
        }

        offScreen->iblRemove();
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
        auto texture = controllers::EditorTextureController::loadTexture(path);

        if (!texture) {
            return EditorTextureHandle{};
        }

        EditorTextureHandle handle;
        handle.imguiDescriptorSet = texture->getDescriptorSet();
        handle.width = static_cast<uint32_t>(texture->getWidth());
        handle.height = static_cast<uint32_t>(texture->getHeight());

        // Track for cleanup
        loadedTextures[handle.imguiDescriptorSet] = std::move(texture);

        return handle;
    }

    EditorTextureHandle RenderServiceImpl::loadEditorHDRTexture(const std::string& path) {
        auto texture = controllers::EditorTextureController::loadHdrTexture(path);

        if (!texture) {
            return EditorTextureHandle{};
        }

        EditorTextureHandle handle;
        handle.imguiDescriptorSet = texture->getDescriptorSet();
        handle.width = static_cast<uint32_t>(texture->getWidth());
        handle.height = static_cast<uint32_t>(texture->getHeight());

        // Track for cleanup
        loadedTextures[handle.imguiDescriptorSet] = std::move(texture);

        return handle;
    }

    void RenderServiceImpl::releaseEditorTexture(const EditorTextureHandle& handle) {
        loadedTextures.erase(handle.imguiDescriptorSet);
    }

    bool RenderServiceImpl::isReady() const {
        return offScreen != nullptr;
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
    }

}
