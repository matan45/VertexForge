#include "RenderTextureController.hpp"
#include "../render/RenderTextureViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../core/VulkanContext.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "print/Log.hpp"

namespace controllers
{
    RenderTextureController::RenderTextureController()
        : device{*core::VulkanContext::getDevice()}
        , swapChain{*core::VulkanContext::getSwapChain()}
    {
    }

    RenderTextureController::~RenderTextureController()
    {
        if (viewport)
        {
            cleanUp();
        }
    }

    void RenderTextureController::init(const rendertexture::RenderTextureDesc& description)
    {
        desc = description;

        viewport = std::make_unique<render::RenderTextureViewPort>(device, swapChain);
        viewport->setClearColor(desc.clearColor);
        viewport->setRenderShadows(desc.renderShadows);
        viewport->init(desc.width, desc.height);
    }

    void RenderTextureController::cleanUp()
    {
        if (viewport)
        {
            viewport->cleanUp();
            viewport.reset();
        }
    }

    void RenderTextureController::updateCamera(const glm::mat4& view, const glm::mat4& proj,
                                                const glm::vec3& pos, float nearVal, float farVal)
    {
        viewMatrix = view;
        projectionMatrix = proj;
        cameraPosition = pos;
        nearPlane = nearVal;
        farPlane = farVal;
    }

    void* RenderTextureController::render(render::RenderPassHandler* mainPassHandler)
    {
        if (!viewport || !enabled)
            return nullptr;

        vk::DescriptorSet result = viewport->render(
            mainPassHandler,
            viewMatrix,
            projectionMatrix,
            cameraPosition,
            nearPlane,
            farPlane
        );

        lastRenderedHandle = static_cast<void*>(result);

        // External-texture registration is owned exclusively by RenderTextureAdapter::renderAll's
        // per-frame repoint loop. It runs unconditionally for every enabled controller after this
        // function returns, so doing the registration here would be (a) redundant and (b) unsafe
        // on early-return paths in viewport->render where lastRenderedImageIndex was never
        // updated to the current swapchain slot.
        return lastRenderedHandle;
    }

    vk::ImageView RenderTextureController::getLatestImageView() const
    {
        return viewport ? viewport->getLatestImageView() : vk::ImageView{};
    }

    vk::Sampler RenderTextureController::getTextureSampler() const
    {
        return viewport ? viewport->getTextureSampler() : vk::Sampler{};
    }

    vk::Semaphore RenderTextureController::getLastRenderCompleteSemaphore() const
    {
        return viewport ? viewport->getLastRenderCompleteSemaphore() : vk::Semaphore{};
    }

    bool RenderTextureController::didSubmitLastRender() const
    {
        return viewport && viewport->didSubmitLastRender();
    }

    bool RenderTextureController::shouldRenderThisFrame(float deltaTime)
    {
        if (!enabled)
            return false;

        switch (desc.updateMode)
        {
        case rendertexture::UpdateMode::EveryFrame:
            return true;

        case rendertexture::UpdateMode::OnDemand:
            if (renderRequested)
            {
                renderRequested = false;
                return true;
            }
            return false;

        case rendertexture::UpdateMode::FixedInterval:
            timeSinceLastRender += deltaTime;
            if (timeSinceLastRender >= desc.fixedIntervalSeconds)
            {
                timeSinceLastRender -= desc.fixedIntervalSeconds;
                // Clamp to avoid spiral-of-death if frames are very slow
                if (timeSinceLastRender > desc.fixedIntervalSeconds)
                    timeSinceLastRender = 0.0f;
                return true;
            }
            return false;
        }

        return true;
    }

    void RenderTextureController::resize(uint32_t w, uint32_t h)
    {
        desc.width = w;
        desc.height = h;

        if (viewport)
        {
            viewport->resize(w, h);
        }
    }
}
