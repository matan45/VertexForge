#pragma once

#include "FrameSynchronizer.hpp"
#include <functional>
#include <thread>
#include <memory>

namespace core
{
    class RenderManager;

    /// Dedicated render thread that runs the Vulkan command recording and submission loop.
    /// The main thread signals frame readiness via FrameSynchronizer; the render thread
    /// picks up the frame data and renders.
    class RenderThread
    {
    public:
        using RenderCallback = std::function<void(uint32_t frameSlot)>;

        RenderThread();
        ~RenderThread();

        RenderThread(const RenderThread&) = delete;
        RenderThread& operator=(const RenderThread&) = delete;

        /// Start the render thread. The callback is invoked each frame with the slot index.
        void start(RenderCallback callback);

        /// Stop the render thread and join. Must be called before Vulkan cleanup.
        void stop();

        bool isRunning() const { return running; }

        FrameSynchronizer& getSynchronizer() { return synchronizer; }

    private:
        void threadLoop();

        FrameSynchronizer synchronizer;
        RenderCallback renderCallback;
        std::thread thread;
        bool running = false;
    };
}
