#pragma once
#include "../core/VulkanMemoryManager.hpp"
#include "../../services/data/ViewportReadbackTypes.hpp"
#include <vulkan/vulkan.hpp>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace core
{
    class Device;
}

namespace render
{
    // VK-1651: one-shot CPU copy of the editor viewport's final colour image (MCP
    // viewport_screenshot). Owned by OffScreenViewPort.
    //
    // Requests and takes arrive on the main thread; recording and completion run on
    // the render thread, so every state transition happens under `mutex`. The
    // request walks Idle -> Requested -> Recorded -> Ready:
    //   - Requested: waits settleFrames more render() calls, so a frame prepared
    //     before the request (one-frame latency) is never the one captured.
    //   - Recorded: the copy is in a command buffer submitted with the in-flight
    //     fence of `slot`; it completes when render() next waits that same fence.
    //   - Ready: the raw RGBA16F halves are on the CPU, handed over once by take().
    // A new request replaces the pending one (the old ticket then reads Failed). A
    // copy already in flight keeps its buffer until its fence is waited, and no new
    // copy is recorded until then, so at most one readback buffer exists at a time.
    class ViewportReadback
    {
    public:
        explicit ViewportReadback(core::Device& device);
        ~ViewportReadback() = default;

        ViewportReadback(const ViewportReadback&) = delete;
        ViewportReadback& operator=(const ViewportReadback&) = delete;

        // OffScreenViewPort::init/cleanUp bracket the window in which a request can
        // be served; a request outside it fails immediately.
        void setAvailable(bool available);

        // Main thread. Returns a new ticket (never 0).
        uint64_t request(uint32_t settleFrames);
        // Main thread. Pending / Ready (pixels moved out, ticket consumed) / Failed.
        services::ViewportReadbackResult take(uint64_t ticket);

        // Render thread, right after waitForFences on `frameSlot`: completes a copy
        // recorded in that slot.
        void onFenceWaited(uint32_t frameSlot);
        // Render thread, after the frame's draw and before the command buffer ends:
        // records the copy once the request has settled. `image` must be in
        // `currentLayout` and is returned to exactly that layout.
        void recordIfArmed(vk::CommandBuffer commandBuffer, vk::Image image, vk::ImageLayout currentLayout,
                           vk::Extent2D extent, uint32_t frameSlot);

        // After a device waitIdle (recreate): a recorded copy is complete, so finish it;
        // a request that has not been recorded yet is kept.
        void onRecreate();
        // After a device waitIdle (cleanUp): drops any buffer and fails the pending request.
        void cleanUp();

    private:
        enum class Stage : uint8_t { Idle, Requested, Recorded, Ready, Failed };

        struct InFlightCopy
        {
            vk::Buffer buffer{};
            core::VulkanAllocation allocation{};
            uint64_t ticket = 0;
            uint32_t slot = 0;
            uint32_t width = 0;
            uint32_t height = 0;
        };

        core::Device& device;

        std::mutex mutex;
        // Mirrors "a request is waiting or a copy is in flight" so the per-frame render
        // thread hooks skip the lock while idle. Written only under `mutex`.
        std::atomic<bool> busy{false};

        bool available = false;
        uint64_t nextTicket = 1;
        uint64_t currentTicket = 0;
        Stage stage = Stage::Idle;
        uint32_t framesUntilArmed = 0;

        InFlightCopy inFlight;

        uint32_t readyWidth = 0;
        uint32_t readyHeight = 0;
        std::vector<uint16_t> readyPixels;

        // All below require `mutex` held.
        void finishInFlightLocked();
        void destroyInFlightLocked();
        void updateBusyLocked();
        vk::MemoryPropertyFlags pickReadbackMemoryProperties(vk::DeviceSize size) const;
    };
}
