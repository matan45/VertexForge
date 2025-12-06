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

}
