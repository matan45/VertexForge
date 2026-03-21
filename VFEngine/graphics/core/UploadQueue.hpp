#pragma once

#include <vulkan/vulkan.hpp>
#include <functional>
#include <mutex>
#include <vector>

namespace core
{
    /// Thread-safe queue for GPU upload requests.
    /// Main thread pushes upload work items, render thread drains and executes them
    /// before the main render pass.
    ///
    /// Upload work items are lambdas that receive a vk::CommandBuffer and record
    /// transfer/upload commands into it.
    class UploadQueue
    {
    public:
        using UploadWork = std::function<void(vk::CommandBuffer cmd)>;

        UploadQueue() = default;
        ~UploadQueue() = default;

        UploadQueue(const UploadQueue&) = delete;
        UploadQueue& operator=(const UploadQueue&) = delete;

        /// Push an upload request (thread-safe, called from any thread).
        void push(UploadWork work)
        {
            std::lock_guard lock(mutex);
            pending.push_back(std::move(work));
        }

        /// Drain all pending uploads and record them into the command buffer.
        /// Called from the render thread before the main render pass.
        /// Returns the number of uploads processed.
        uint32_t drain(vk::CommandBuffer cmd)
        {
            std::vector<UploadWork> local;
            {
                std::lock_guard lock(mutex);
                local.swap(pending);
            }

            for (auto& work : local)
            {
                work(cmd);
            }

            return static_cast<uint32_t>(local.size());
        }

        /// Check if there are pending uploads (thread-safe).
        bool hasPending() const
        {
            std::lock_guard lock(mutex);
            return !pending.empty();
        }

        /// Get count of pending uploads (thread-safe).
        size_t getPendingCount() const
        {
            std::lock_guard lock(mutex);
            return pending.size();
        }

    private:
        mutable std::mutex mutex;
        std::vector<UploadWork> pending;
    };
}
