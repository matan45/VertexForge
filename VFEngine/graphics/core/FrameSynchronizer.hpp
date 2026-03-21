#pragma once

#include "RenderManager.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace core
{
    /// Manages frame handoff between the main (game) thread and the render thread.
    ///
    /// Ensures strict sequencing: the main thread cannot start a new frame until
    /// the render thread finishes the previous one. This is required because ImGui
    /// has a single global context (NewFrame/Render must not overlap).
    ///
    /// The CPU/GPU overlap still happens: while the GPU executes frame N's commands
    /// (after the render thread submits them), the CPU prepares frame N+1's data.
    class FrameSynchronizer
    {
    public:
        FrameSynchronizer() = default;
        ~FrameSynchronizer() = default;

        FrameSynchronizer(const FrameSynchronizer&) = delete;
        FrameSynchronizer& operator=(const FrameSynchronizer&) = delete;

        /// Called by the main thread at the start of a frame.
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
        /// Returns the current frame number.
        uint64_t waitForFrame()
        {
            std::unique_lock lock(mutex);
            cv.wait(lock, [&] { return frameReady || stopRequested; });
            frameReady = false;
            return frameNumber;
        }

        /// Called by the render thread after rendering is complete.
        /// Unblocks the main thread's beginFrame().
        void frameComplete()
        {
            {
                std::lock_guard lock(mutex);
                renderInProgress = false;
            }
            cv.notify_all();
        }

        /// Request the render thread to stop (for clean shutdown).
        void requestStop()
        {
            {
                std::lock_guard lock(mutex);
                stopRequested = true;
            }
            cv.notify_all();
        }

        bool isStopRequested() const { return stopRequested; }
        uint64_t getFrameNumber() const { return frameNumber; }

    private:
        std::mutex mutex;
        std::condition_variable cv;
        bool frameReady = false;
        bool renderInProgress = false;
        uint64_t frameNumber = 0;
        std::atomic<bool> stopRequested{false};
    };
}
