#include "RenderTextureAdapter.hpp"
#include "../../graphics/controllers/RenderTextureController.hpp"
#include "../../graphics/render/RenderPassHandler.hpp"
#include "../../graphics/core/RenderManager.hpp"
#include "../../graphics/render/OffScreenViewPort.hpp"
#include "../../controllers/OffScreen.hpp"
#include <algorithm>
#include <cassert>
#include <vector>

namespace core
{
    RenderTextureAdapter::RenderTextureAdapter(::controllers::OffScreen* offScreen)
        : mainOffScreen(offScreen)
    {
    }

    RenderTextureAdapter::~RenderTextureAdapter() noexcept = default;

    ::controllers::RenderTextureController* RenderTextureAdapter::getController(
        rendertexture::RenderTextureId id) const
    {
        auto it = controllers.find(id);
        return (it != controllers.end()) ? it->second.get() : nullptr;
    }

    render::RenderPassHandler* RenderTextureAdapter::getMainRenderPassHandler() const
    {
        if (!mainOffScreen)
            return nullptr;
        return mainOffScreen->getRenderPassHandler();
    }

    rendertexture::RenderTextureId RenderTextureAdapter::createRenderTexture(
        const rendertexture::RenderTextureDesc& desc)
    {
        rendertexture::RenderTextureId id = nextId++;
        assert(nextId != 0 && "RenderTextureId overflow — 4 billion create/destroy cycles exceeded");

        auto controller = std::make_unique<::controllers::RenderTextureController>();
        controller->init(desc);
        controller->setTextureKey("__rtt_" + std::to_string(id) + "__");

        std::lock_guard lock(controllersMutex);
        controllers[id] = std::move(controller);
        return id;
    }

    void RenderTextureAdapter::destroyRenderTexture(rendertexture::RenderTextureId id)
    {
        std::lock_guard lock(controllersMutex);

        auto it = controllers.find(id);
        if (it == controllers.end())
            return;

        // Drop the UI/Billboard external-texture cache entry BEFORE destroying the underlying
        // vk::ImageView/vk::Sampler. Without this the descriptor sets in those caches retain
        // dangling handles; any subsequent UIImage referencing __rtt_<id>__ would sample freed
        // memory. controller->cleanUp() already does device.waitIdle() inside viewport->cleanUp,
        // so it is safe to assume no GPU command buffer references the descriptor after we
        // unregister and then tear down the views.
        if (it->second)
        {
            const auto& key = it->second->getTextureKey();
            if (!key.empty())
            {
                if (auto* passHandler = getMainRenderPassHandler())
                {
                    passHandler->unregisterExternalTexture(key);
                }
            }
            it->second->cleanUp();
        }
        controllers.erase(it);
    }

    void RenderTextureAdapter::updateCamera(rendertexture::RenderTextureId id,
        const glm::mat4& view, const glm::mat4& proj,
        const glm::vec3& pos, float nearPlane, float farPlane, uint32_t cullingMask)
    {
        std::lock_guard lock(controllersMutex);
        auto* controller = getController(id);
        if (controller)
        {
            controller->updateCamera(view, proj, pos, nearPlane, farPlane, cullingMask);
        }
    }

    void RenderTextureAdapter::renderAll(float deltaTime)
    {
        auto* passHandler = getMainRenderPassHandler();
        if (!passHandler)
        {
            return;
        }

        // Hold the mutex for the full body: the local `enabled`/`toRender` vectors hold raw
        // RenderTextureController* into the map's owned unique_ptrs, and we call render() on
        // each (which submits + CPU-waits for GPU). If we released the lock between snapshot
        // and use, the main thread could destroy a controller (Stop button → exitPlayMode →
        // destroyRenderTexture) and leave us dereferencing freed memory. Mutator contention is
        // limited to play-mode transitions and explicit resize/setEnabled calls — those will
        // block until the current renderAll frame finishes, which is acceptable.
        std::lock_guard lock(controllersMutex);

        std::vector<std::pair<rendertexture::RenderTextureId, ::controllers::RenderTextureController*>> enabled;
        std::vector<std::pair<rendertexture::RenderTextureId, ::controllers::RenderTextureController*>> toRender;
        enabled.reserve(controllers.size());

        for (auto& [id, ctrl] : controllers)
        {
            if (ctrl && ctrl->isEnabled())
            {
                enabled.emplace_back(id, ctrl.get());

                if (ctrl->shouldRenderThisFrame(deltaTime))
                {
                    toRender.emplace_back(id, ctrl.get());
                }
            }
        }

        // Register RTT camera frustums for ALL enabled cameras so terrain
        // tiles are loaded even when the camera is between renders (FixedInterval).
        if (mainOffScreen)
        {
            mainOffScreen->clearAdditionalTerrainFrustums();
            for (auto& [id, ctrl] : enabled)
            {
                glm::mat4 vp = ctrl->getProjectionMatrix() * ctrl->getViewMatrix();
                mainOffScreen->addTerrainFrustum(vp, ctrl->getCameraPosition());
            }
        }

        std::sort(toRender.begin(), toRender.end(),
            [](const auto& a, const auto& b)
            {
                return a.second->getPriority() < b.second->getPriority();
            });

        for (auto& [id, ctrl] : toRender)
        {
            ctrl->render(passHandler);
            if (ctrl->didSubmitLastRender())
            {
                render::OffScreenViewPort::addPendingRenderWait({
                    ctrl->getLastRenderCompleteSemaphore(),
                    vk::PipelineStageFlagBits::eFragmentShader,
                    0
                });
            }
        }

        // VK-1334 minimap flicker fix: for every enabled controller (including those that did
        // NOT render this frame), point the external descriptor for the current swapchain slot
        // at the most recently produced RTT view. Cold-slot images were pre-cleared to clearColor
        // at viewport init so this is always a valid image to sample. Safe to update slot I here:
        // RenderManager already waited imagesInFlight[I] before invoking preRenderCallback, so no
        // in-flight UI command buffer is referencing the descriptor for slot I.
        const uint32_t currentImageIndex = ::core::RenderManager::getImageIndex();
        for (auto& [id, ctrl] : enabled)
        {
            const auto& key = ctrl->getTextureKey();
            if (key.empty()) continue;

            auto sampler = ctrl->getTextureSampler();
            auto view = ctrl->getLatestImageView();
            if (!sampler || !view) continue;

            passHandler->registerExternalTexture(key, currentImageIndex, view, sampler);
        }
    }

    void* RenderTextureAdapter::getTextureHandle(rendertexture::RenderTextureId id) const
    {
        std::lock_guard lock(controllersMutex);
        auto* controller = getController(id);
        return controller ? controller->getLastRenderedHandle() : nullptr;
    }

    uint32_t RenderTextureAdapter::getWidth(rendertexture::RenderTextureId id) const
    {
        std::lock_guard lock(controllersMutex);
        auto* controller = getController(id);
        return controller ? controller->getWidth() : 0;
    }

    uint32_t RenderTextureAdapter::getHeight(rendertexture::RenderTextureId id) const
    {
        std::lock_guard lock(controllersMutex);
        auto* controller = getController(id);
        return controller ? controller->getHeight() : 0;
    }

    bool RenderTextureAdapter::isValid(rendertexture::RenderTextureId id) const
    {
        std::lock_guard lock(controllersMutex);
        return getController(id) != nullptr;
    }

    void RenderTextureAdapter::resize(rendertexture::RenderTextureId id, uint32_t w, uint32_t h)
    {
        std::lock_guard lock(controllersMutex);

        auto* controller = getController(id);
        if (!controller)
            return;

        // viewport->resize destroys the per-slot color images (and their vk::ImageView handles)
        // before allocating new ones. Drop the UI/Billboard cache entry first so non-current
        // swapchain slots don't keep descriptors pointing at the about-to-be-freed views — the
        // next render() and the adapter's repoint loop will re-register the fresh views.
        const auto& key = controller->getTextureKey();
        if (!key.empty())
        {
            if (auto* passHandler = getMainRenderPassHandler())
            {
                passHandler->unregisterExternalTexture(key);
            }
        }

        controller->resize(w, h);
    }

    void RenderTextureAdapter::setEnabled(rendertexture::RenderTextureId id, bool enabled)
    {
        std::lock_guard lock(controllersMutex);
        auto* controller = getController(id);
        if (controller)
        {
            controller->setEnabled(enabled);
        }
    }

    void RenderTextureAdapter::setUpdateMode(rendertexture::RenderTextureId id,
                                              rendertexture::UpdateMode mode)
    {
        std::lock_guard lock(controllersMutex);
        auto* controller = getController(id);
        if (controller)
        {
            controller->setUpdateMode(mode);
        }
    }

    void RenderTextureAdapter::requestRender(rendertexture::RenderTextureId id)
    {
        std::lock_guard lock(controllersMutex);
        auto* controller = getController(id);
        if (controller)
        {
            controller->requestRender();
        }
    }

    std::vector<services::RenderTextureDebugInfo> RenderTextureAdapter::getActiveRenderTextures() const
    {
        std::lock_guard lock(controllersMutex);

        std::vector<services::RenderTextureDebugInfo> result;
        result.reserve(controllers.size());

        for (const auto& [id, ctrl] : controllers)
        {
            if (!ctrl)
                continue;

            services::RenderTextureDebugInfo info;
            info.textureId = id;
            info.width = ctrl->getWidth();
            info.height = ctrl->getHeight();
            info.updateMode = static_cast<uint8_t>(ctrl->getUpdateMode());
            info.priority = ctrl->getPriority();
            info.enabled = ctrl->isEnabled();
            info.hasRendered = ctrl->getLastRenderedHandle() != nullptr;
            info.submittedLastFrame = ctrl->didSubmitLastRender();
            result.push_back(info);
        }

        // Sort ascending by priority so the returned order matches the render order in renderAll().
        std::sort(result.begin(), result.end(),
            [](const services::RenderTextureDebugInfo& a, const services::RenderTextureDebugInfo& b)
            {
                return a.priority < b.priority;
            });

        return result;
    }
}
