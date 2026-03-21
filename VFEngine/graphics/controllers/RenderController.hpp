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
    class RenderThread;
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
        std::unique_ptr<core::RenderThread> renderThread;
        std::function<void()> preRenderCallback;
        bool useRenderThread = false;

    public:
        explicit RenderController(bool imguiEnabled = true);
        ~RenderController();

        void init();
        void cleanUp();

        void reSize();

        /// In single-threaded mode: renders immediately.
        /// In render-thread mode: signals the render thread that a frame is ready.
        void render();

        /// Called at the start of each frame to block if the render thread
        /// hasn't finished consuming the previous frame's slot.
        void beginFrame();

        void setResizeCallback(core::ResizeCallback callback);
        void setBlitSourceProvider(core::BlitSourceProvider provider);

        /// Enable/disable the dedicated render thread. Must be called before init().
        void setUseRenderThread(bool enabled) { useRenderThread = enabled; }

        /// Set a callback to run on the render thread BEFORE swapchain present.
        /// Use this to move GPU work (e.g. offscreen scene rendering) to the render thread.
        void setPreRenderCallback(std::function<void()> callback) { preRenderCallback = std::move(callback); }

        /// Main thread: generate ImGui draw data and snapshot for render thread.
        void snapshotImGuiDrawData();

        bool isRenderThreadEnabled() const { return useRenderThread; }
    };
}
