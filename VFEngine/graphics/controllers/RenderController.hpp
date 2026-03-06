#pragma once
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>
#include <memory>
#include <functional>

namespace window
{
    class Window;
}

namespace core
{
    class SwapChain;
    class Device;
    class RenderManager;
    using ResizeCallback = std::function<void()>;
    using BlitSourceProvider = std::function<vk::Image(uint32_t imageIndex)>;
}

namespace controllers
{
    class RenderController
    {
    private:
        const window::Window* window; // Non-owning pointer
        core::SwapChain& swapChain;
        core::Device& device;
        std::unique_ptr<core::RenderManager> renderManager;

    public:
        explicit RenderController(bool imguiEnabled = true);
        ~RenderController();

        void init();
        void cleanUp() const;

        void reSize();

        void render();

        void setResizeCallback(core::ResizeCallback callback);
        void setBlitSourceProvider(core::BlitSourceProvider provider);
    };
}
