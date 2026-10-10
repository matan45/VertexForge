#pragma once

#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace mcp
{
    // The main thread did not run the task within the caller's timeout. The task
    // was marked cancelled; if drain() had already started it, it still completes.
    class MainThreadTimeout : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // The queue was shut down (editor exiting) before the task ran.
    class QueueShutdown : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // Marshals MCP tool work from HTTP connection threads onto the editor main
    // thread. EventDispatcher handlers mutate EnTT / ImGui state and are only safe
    // there; EditorHandler drains this queue once per frame at the TOP of its frame
    // callback, before the frame task graph runs, while the render thread is idle
    // (VK-1653). A task must therefore never wait for a rendered frame: nothing
    // renders until the drain returns.
    class MainThreadQueue
    {
    public:
        using Task = std::function<nlohmann::json()>;

        // Called from a worker thread. Blocks until the main thread has run `task`
        // (returning its value or rethrowing its exception), or throws
        // MainThreadTimeout / QueueShutdown.
        nlohmann::json invoke(Task task, std::chrono::milliseconds timeout);

        // Called from the main thread. Runs queued tasks until the queue is empty
        // or `budget` has elapsed (at least one task always runs when queued).
        // Returns the number of tasks executed.
        std::size_t drain(std::chrono::milliseconds budget);

        // Fails all pending tasks and every later invoke() with QueueShutdown.
        void shutdown();

        // Re-arms a queue after shutdown() (server restarted from preferences).
        void reset();

        std::size_t pending() const;

    private:
        struct Item
        {
            Task task;
            std::promise<nlohmann::json> promise;
            std::atomic<bool> cancelled{false};
        };

        mutable std::mutex mutex;
        std::deque<std::shared_ptr<Item>> items;
        bool closed = false;
    };
}
