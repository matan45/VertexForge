#pragma once
#include <memory>

namespace window
{
    class Window;
}

namespace core
{
    class SwapChain;
    class Device;
    class RenderManager;
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
        explicit RenderController();
        ~RenderController();

        void init();
        void cleanUp() const;

        void reSize() const;

        void render();
    };
}
