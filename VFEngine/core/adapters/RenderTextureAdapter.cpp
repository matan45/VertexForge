#include "RenderTextureAdapter.hpp"
#include "../../graphics/controllers/RenderTextureController.hpp"
#include "../controllers/OffScreen.hpp"
#include "../../graphics/render/RenderPassHandler.hpp"
#include "print/Logger.hpp"
#include <algorithm>
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

        auto controller = std::make_unique<::controllers::RenderTextureController>();
        controller->init(desc);

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
        const glm::vec3& pos, float near, float far)
    {
        auto* controller = getController(id);
        if (controller)
        {
            controller->updateCamera(view, proj, pos, near, far);
        }
    }

    void RenderTextureAdapter::renderAll(float deltaTime)
    {
        auto* passHandler = getMainRenderPassHandler();
        if (!passHandler)
            return;

        // Collect enabled controllers and sort by priority
        std::vector<std::pair<rendertexture::RenderTextureId, ::controllers::RenderTextureController*>> sorted;
        sorted.reserve(controllers.size());

        for (auto& [id, ctrl] : controllers)
        {
            if (ctrl && ctrl->isEnabled())
            {
                sorted.emplace_back(id, ctrl.get());
            }
        }

        std::sort(sorted.begin(), sorted.end(),
            [](const auto& a, const auto& b)
            {
                return a.second->getPriority() < b.second->getPriority();
            });

        // Render each RTT camera sequentially (they share GPUDrivenRenderer state)
        for (auto& [id, ctrl] : sorted)
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
        // For OnDemand mode - flag the controller to render next frame
        // This will be handled during renderAll()
        auto* controller = getController(id);
        if (controller)
        {
            controller->setEnabled(true);
        }
    }
}
