#include "MainThreadQueue.hpp"

namespace mcp
{
    nlohmann::json MainThreadQueue::invoke(Task task, std::chrono::milliseconds timeout)
    {
        auto item = std::make_shared<Item>();
        item->task = std::move(task);
        std::future<nlohmann::json> future = item->promise.get_future();

        {
            std::lock_guard<std::mutex> lock(mutex);
            if (closed)
            {
                throw QueueShutdown("editor is shutting down");
            }
            items.push_back(item);
        }

        if (future.wait_for(timeout) != std::future_status::ready)
        {
            // drain() checks this flag before running the task. If it already
            // started, the result is simply dropped (the promise outlives us via
            // the shared Item).
            item->cancelled.store(true, std::memory_order_release);
            throw MainThreadTimeout("editor main thread did not respond within " +
                                    std::to_string(timeout.count()) +
                                    " ms (a modal dialog, breakpoint or long load may be blocking it); "
                                    "the request was cancelled if it had not started yet");
        }
        return future.get();
    }

    std::size_t MainThreadQueue::drain(std::chrono::milliseconds budget)
    {
        const auto start = std::chrono::steady_clock::now();
        std::size_t executed = 0;

        for (;;)
        {
            std::shared_ptr<Item> item;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (items.empty())
                {
                    break;
                }
                item = std::move(items.front());
                items.pop_front();
            }

            if (item->cancelled.load(std::memory_order_acquire))
            {
                continue;  // the caller timed out; nobody reads the promise
            }

            try
            {
                item->promise.set_value(item->task());
            }
            catch (...)
            {
                item->promise.set_exception(std::current_exception());
            }
            ++executed;

            if (std::chrono::steady_clock::now() - start >= budget)
            {
                break;
            }
        }
        return executed;
    }

    void MainThreadQueue::shutdown()
    {
        std::deque<std::shared_ptr<Item>> orphaned;
        {
            std::lock_guard<std::mutex> lock(mutex);
            closed = true;
            orphaned.swap(items);
        }
        for (auto& item : orphaned)
        {
            item->promise.set_exception(std::make_exception_ptr(QueueShutdown("editor is shutting down")));
        }
    }

    void MainThreadQueue::reset()
    {
        std::lock_guard<std::mutex> lock(mutex);
        closed = false;
    }

    std::size_t MainThreadQueue::pending() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return items.size();
    }
}
