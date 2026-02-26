#include "RenderTextureAdapter.hpp"
#include "../../graphics/controllers/RenderTextureController.hpp"
#include "../controllers/OffScreen.hpp"
#include "print/Logger.hpp"
#include <algorithm>
#include <cassert>
#include <vector>

namespace core
{
    RenderTextureAdapter::RenderTextureAdapter(::controllers::OffScreen* offScreen)
        : mainOffScreen(offScreen)
    {
    }

    RenderTextureAdapter::~RenderTextureAdapter() noexcept
    {
        controllers.clear();
    }

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

        controllers[id] = std::move(controller);
        return id;
    }

    void RenderTextureAdapter::destroyRenderTexture(rendertexture::RenderTextureId id)
    {
        auto it = controllers.find(id);
        if (it != controllers.end())
        {
            if (it->second)
            {
                it->second->cleanUp();
            }
            controllers.erase(it);
        }
    }

    void RenderTextureAdapter::updateCamera(rendertexture::RenderTextureId id,
        const glm::mat4& view, const glm::mat4& proj,
        const glm::vec3& pos, float nearPlane, float farPlane)
    {
        auto* controller = getController(id);
        if (controller)
        {
            controller->updateCamera(view, proj, pos, nearPlane, farPlane);
        }
    }

    void RenderTextureAdapter::renderAll(float deltaTime)
    {
        auto* passHandler = getMainRenderPassHandler();
        if (!passHandler)
        {
            return;
        }

        // Collect all enabled controllers (for frustum registration)
        // and determine which actually need to render this frame (based on UpdateMode).
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

        // Register RTT camera frustums for ALL enabled cameras so terrain/water
        // tiles are loaded even when the camera is between renders (FixedInterval).
        if (mainOffScreen)
        {
            mainOffScreen->clearAdditionalTerrainFrustums();
            mainOffScreen->clearAdditionalWaterFrustums();
            for (auto& [id, ctrl] : enabled)
            {
                glm::mat4 vp = ctrl->getProjectionMatrix() * ctrl->getViewMatrix();
                mainOffScreen->addTerrainFrustum(vp, ctrl->getCameraPosition());
                mainOffScreen->addWaterFrustum(vp, ctrl->getCameraPosition());
            }
        }

        // Sort by priority and render only those that need it this frame
        std::sort(toRender.begin(), toRender.end(),
            [](const auto& a, const auto& b)
            {
                return a.second->getPriority() < b.second->getPriority();
            });

        for (auto& [id, ctrl] : toRender)
        {
            ctrl->render(passHandler);
        }
    }

    void* RenderTextureAdapter::getTextureHandle(rendertexture::RenderTextureId id) const
    {
        auto* controller = getController(id);
        return controller ? controller->getLastRenderedHandle() : nullptr;
    }

    uint32_t RenderTextureAdapter::getWidth(rendertexture::RenderTextureId id) const
    {
        auto* controller = getController(id);
        return controller ? controller->getWidth() : 0;
    }

    uint32_t RenderTextureAdapter::getHeight(rendertexture::RenderTextureId id) const
    {
        auto* controller = getController(id);
        return controller ? controller->getHeight() : 0;
    }

    bool RenderTextureAdapter::isValid(rendertexture::RenderTextureId id) const
    {
        return getController(id) != nullptr;
    }

    void RenderTextureAdapter::resize(rendertexture::RenderTextureId id, uint32_t w, uint32_t h)
    {
        auto* controller = getController(id);
        if (controller)
        {
            controller->resize(w, h);
        }
    }

    void RenderTextureAdapter::setEnabled(rendertexture::RenderTextureId id, bool enabled)
    {
        auto* controller = getController(id);
        if (controller)
        {
            controller->setEnabled(enabled);
        }
    }

    void RenderTextureAdapter::setUpdateMode(rendertexture::RenderTextureId id,
                                              rendertexture::UpdateMode mode)
    {
        auto* controller = getController(id);
        if (controller)
        {
            controller->setUpdateMode(mode);
        }
    }

    void RenderTextureAdapter::requestRender(rendertexture::RenderTextureId id)
    {
        auto* controller = getController(id);
        if (controller)
        {
            controller->requestRender();
        }
    }
}
