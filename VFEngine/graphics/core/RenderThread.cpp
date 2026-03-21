#include "RenderThread.hpp"
#include "print/Log.hpp"

namespace core
{
    RenderThread::RenderThread() = default;

    RenderThread::~RenderThread()
    {
        stop();
    }

    void RenderThread::start(RenderCallback callback)
    {
        if (running)
        {
            vfLogWarning("RenderThread: Already running");
            return;
        }

        renderCallback = std::move(callback);
        running = true;

        thread = std::thread(&RenderThread::threadLoop, this);

        vfLogInfo("RenderThread: Started");
    }

    void RenderThread::stop()
    {
        if (!running)
            return;

        synchronizer.requestStop();

        if (thread.joinable())
        {
            thread.join();
        }

        running = false;
        vfLogInfo("RenderThread: Stopped");
    }

    void RenderThread::threadLoop()
    {
        while (!synchronizer.isStopRequested())
        {
            uint32_t slot = synchronizer.waitForFrame();

            if (synchronizer.isStopRequested())
                break;

            if (renderCallback)
            {
                renderCallback(slot);
            }

            synchronizer.frameComplete(slot);
        }
    }
}
