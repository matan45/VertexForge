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

        if (!textureKey.empty() && mainPassHandler)
        {
            auto texSampler = viewport->getTextureSampler();
            if (texSampler)
            {
                for (uint32_t imageIndex = 0; imageIndex < viewport->getImageCount(); ++imageIndex)
                {
                    auto imageView = viewport->getImageView(imageIndex);
                    if (imageView)
                    {
                        mainPassHandler->registerExternalTexture(
                            textureKey,
                            imageIndex,
                            imageView,
                            texSampler);
                    }
                }
            }
        }

        return lastRenderedHandle;
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
