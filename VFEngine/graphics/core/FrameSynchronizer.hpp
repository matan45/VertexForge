#pragma once

#include "GraphicsConstants.hpp"
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace core
{
    /// Manages frame handoff between the main (game) thread and the render thread.
    ///
    /// ImGui requires sequential access (NewFrame/Render must not overlap), so
    /// beginFrame() blocks until the render thread finishes. However, beginFrame()
    /// is called INSIDE the ImGuiDraw task (not at the start of the frame), so
    /// scene update tasks run in parallel with the render thread.
    ///
    /// Timeline:
    ///   Main:   [Scene+Physics+Scripts] [beginFrame:WAIT] [ImGui+signal]
    ///   Render:        [OffScreen + Present]                      [OffScreen + Present]
    class FrameSynchronizer
    {
    public:
        FrameSynchronizer() = default;
        ~FrameSynchronizer() = default;

        FrameSynchronizer(const FrameSynchronizer&) = delete;
        FrameSynchronizer& operator=(const FrameSynchronizer&) = delete;

        /// Called by the main thread before ImGui::NewFrame().
        /// Blocks until the render thread has finished the previous frame.
        void beginFrame()
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return !renderInProgress || stopRequested; });
        }

        /// Called by the main thread after frame data is ready.
        /// Signals the render thread that a new frame is available.
        void endFrame()
        {
            {
                std::lock_guard lock(mutex);
                frameReady = true;
                renderInProgress = true;
                frameNumber++;
            }
            cv.notify_all();
        }

        /// Called by the render thread to wait for a new frame.
        /// Returns 0 if stop was requested (caller should exit).
        uint64_t waitForFrame()
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return frameReady || stopRequested; });
            if (stopRequested) return 0;
            frameReady = false;
            return frameNumber;
        }

        /// Called by the render thread after rendering is complete.
        void frameComplete()
        {
            {
                std::lock_guard lock(mutex);
                renderInProgress = false;
            }
            cv.notify_all();
        }

        /// Wait until the render thread is idle (for resize/shutdown).
        void waitUntilIdle()
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return !renderInProgress || stopRequested; });
        }

        void requestStop()
        {
            {
                std::lock_guard lock(mutex);
                stopRequested = true;
            }
            cv.notify_all();
        }

        bool isStopRequested()
        {
            std::lock_guard lock(mutex);
            return stopRequested;
        }

        uint64_t getFrameNumber() const { return frameNumber; }

    private:
        std::mutex mutex;
        std::condition_variable cv;
        bool frameReady = false;
        bool renderInProgress = false;
        bool stopRequested = false;
        uint64_t frameNumber = 0;
    };
}
