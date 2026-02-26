#include "RenderTextureController.hpp"
#include "../render/RenderTextureViewPort.hpp"
#include "../render/RenderPassHandler.hpp"
#include "../core/VulkanContext.hpp"
#include "../core/Device.hpp"
#include "../core/SwapChain.hpp"
#include "print/Logger.hpp"

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

        // Register rendered texture with UI and Billboard pipelines
        if (!textureKey.empty() && mainPassHandler)
        {
            auto imageView = viewport->getLastRenderedImageView();
            auto texSampler = viewport->getTextureSampler();
            if (imageView && texSampler)
            {
                mainPassHandler->registerExternalTexture(textureKey, imageView, texSampler);
            }
        }

        return lastRenderedHandle;
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
