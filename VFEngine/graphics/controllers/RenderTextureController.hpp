#pragma once
#include <rendertexture/RenderTextureTypes.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <memory>

namespace core
{
    class Device;
    class SwapChain;
}

namespace render
{
    class RenderTextureViewPort;
    class RenderPassHandler;
}

namespace controllers
{
    class RenderTextureController
    {
    private:
        core::Device& device;
        core::SwapChain& swapChain;
        std::unique_ptr<render::RenderTextureViewPort> viewport;
        rendertexture::RenderTextureDesc desc;

        glm::mat4 viewMatrix{1.0f};
        glm::mat4 projectionMatrix{1.0f};
        glm::vec3 cameraPosition{0.0f};
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        bool enabled = true;
        bool renderRequested = false;
        float timeSinceLastRender = 0.0f;
        void* lastRenderedHandle = nullptr;
        std::string textureKey;

    public:
        RenderTextureController();
        ~RenderTextureController();

        RenderTextureController(const RenderTextureController&) = delete;
        RenderTextureController& operator=(const RenderTextureController&) = delete;

        void init(const rendertexture::RenderTextureDesc& desc);
        void cleanUp();

        void updateCamera(const glm::mat4& view, const glm::mat4& proj,
                          const glm::vec3& pos, float nearVal, float farVal);

        void* render(render::RenderPassHandler* mainPassHandler);

        void resize(uint32_t w, uint32_t h);

        uint32_t getWidth() const { return desc.width; }
        uint32_t getHeight() const { return desc.height; }
        bool isEnabled() const { return enabled; }
        void setEnabled(bool e) { enabled = e; }
        void setUpdateMode(rendertexture::UpdateMode mode) { desc.updateMode = mode; }
        rendertexture::UpdateMode getUpdateMode() const { return desc.updateMode; }
        uint32_t getPriority() const { return desc.priority; }
        void* getLastRenderedHandle() const { return lastRenderedHandle; }

        // Returns true if this controller should render during this frame,
        // based on updateMode, deltaTime accumulation, and one-shot requests.
        bool shouldRenderThisFrame(float deltaTime);

        // Flag a one-shot render for OnDemand mode (consumed after the next render).
        void requestRender() { renderRequested = true; }

        const glm::mat4& getViewMatrix() const { return viewMatrix; }
        const glm::mat4& getProjectionMatrix() const { return projectionMatrix; }
        const glm::vec3& getCameraPosition() const { return cameraPosition; }

        void setTextureKey(const std::string& key) { textureKey = key; }
    };
}
