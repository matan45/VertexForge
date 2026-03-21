#pragma once

#include "RenderManager.hpp"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace core
{
    /// Manages frame handoff between the main (game) thread and the render thread.
    ///
    /// The main thread writes frame data into slot `N % MAX_FRAMES_IN_FLIGHT`,
    /// then signals the render thread. The render thread picks up the slot,
    /// renders, and signals completion so the main thread can reuse the slot.
    class FrameSynchronizer
    {
    public:
        FrameSynchronizer() = default;
        ~FrameSynchronizer() = default;

        FrameSynchronizer(const FrameSynchronizer&) = delete;
        FrameSynchronizer& operator=(const FrameSynchronizer&) = delete;

        /// Called by the main thread at the start of a frame.
        /// Blocks if the render thread hasn't finished consuming the slot we need.
        void beginFrame()
        {
            uint32_t slot = frameNumber % MAX_FRAMES_IN_FLIGHT;
            std::unique_lock lock(slotMutex[slot]);
            slotCV[slot].wait(lock, [&] { return !slotInUse[slot]; });
            slotInUse[slot] = true;
        }

        /// Called by the main thread after frame data is ready.
        /// Signals the render thread that a new frame is available.
        void endFrame()
        {
            {
                std::lock_guard lock(frameMutex);
                frameReady = true;
                currentSlot = frameNumber % MAX_FRAMES_IN_FLIGHT;
                frameNumber++;
            }
            frameCV.notify_one();
        }

        /// Called by the render thread to wait for a new frame.
        /// Returns the slot index to render.
        uint32_t waitForFrame()
        {
            std::unique_lock lock(frameMutex);
            frameCV.wait(lock, [&] { return frameReady || stopRequested; });
            frameReady = false;
            return currentSlot;
        }

        /// Called by the render thread after rendering is complete.
        /// Frees the slot for the main thread to reuse.
        void frameComplete(uint32_t slot)
        {
            {
                std::lock_guard lock(slotMutex[slot]);
                slotInUse[slot] = false;
            }
            slotCV[slot].notify_one();
        }

        /// Request the render thread to stop (for clean shutdown).
        void requestStop()
        {
            {
                std::lock_guard lock(frameMutex);
                stopRequested = true;
            }
            frameCV.notify_one();
        }

        bool isStopRequested() const { return stopRequested; }

        uint64_t getFrameNumber() const { return frameNumber; }

    private:
        // Per-slot synchronization (main thread waits for slot to be free)
        std::array<std::mutex, MAX_FRAMES_IN_FLIGHT> slotMutex;
        std::array<std::condition_variable, MAX_FRAMES_IN_FLIGHT> slotCV;
        std::array<bool, MAX_FRAMES_IN_FLIGHT> slotInUse{};

        // Frame-level synchronization (render thread waits for new frame)
        std::mutex frameMutex;
        std::condition_variable frameCV;
        bool frameReady = false;
        uint32_t currentSlot = 0;
        uint64_t frameNumber = 0;

        std::atomic<bool> stopRequested{false};
    };
}
