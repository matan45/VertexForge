#include <doctest.h>

#include "dispatch/MainThreadQueue.hpp"

#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

namespace
{
    // Pumps the queue the way EditorHandler's frame callback does until `done`.
    void pumpUntil(mcp::MainThreadQueue& queue, const std::future<void>& done)
    {
        while (done.wait_for(1ms) != std::future_status::ready)
        {
            queue.drain(8ms);
        }
    }
}

TEST_CASE("mcp queue: invoke runs on the draining thread and returns the value")
{
    mcp::MainThreadQueue queue;
    const auto mainId = std::this_thread::get_id();
    std::thread::id ranOn;
    nlohmann::json value;

    std::future<void> done = std::async(std::launch::async, [&]()
    {
        value = queue.invoke([&]()
        {
            ranOn = std::this_thread::get_id();
            return nlohmann::json{{"answer", 42}};
        }, 5000ms);
    });
    pumpUntil(queue, done);
    done.get();

    CHECK(ranOn == mainId);
    CHECK(value["answer"] == 42);
}

TEST_CASE("mcp queue: exceptions propagate to the caller")
{
    mcp::MainThreadQueue queue;
    std::future<void> done = std::async(std::launch::async, [&]()
    {
        CHECK_THROWS_AS(queue.invoke([]() -> nlohmann::json { throw std::runtime_error("bad"); }, 5000ms),
                        std::runtime_error);
    });
    pumpUntil(queue, done);
    done.get();
}

TEST_CASE("mcp queue: timeout cancels a task that has not started")
{
    mcp::MainThreadQueue queue;
    std::atomic<bool> ran{false};

    CHECK_THROWS_AS(queue.invoke([&]() { ran = true; return nlohmann::json(); }, 20ms), mcp::MainThreadTimeout);
    CHECK(queue.pending() == 1);

    // The cancelled item is skipped, not executed.
    CHECK(queue.drain(8ms) == 0);
    CHECK_FALSE(ran.load());
    CHECK(queue.pending() == 0);
}

TEST_CASE("mcp queue: shutdown fails pending and later work")
{
    mcp::MainThreadQueue queue;

    std::future<void> waiter = std::async(std::launch::async, [&]()
    {
        CHECK_THROWS_AS(queue.invoke([]() { return nlohmann::json(); }, 5000ms), mcp::QueueShutdown);
    });
    while (queue.pending() == 0)
    {
        std::this_thread::sleep_for(1ms);
    }
    queue.shutdown();
    waiter.get();

    CHECK_THROWS_AS(queue.invoke([]() { return nlohmann::json(); }, 10ms), mcp::QueueShutdown);

    queue.reset();
    std::future<void> again = std::async(std::launch::async, [&]()
    {
        CHECK(queue.invoke([]() { return nlohmann::json(1); }, 5000ms) == 1);
    });
    pumpUntil(queue, again);
    again.get();
}

TEST_CASE("mcp queue: drain respects the budget but always runs one task")
{
    mcp::MainThreadQueue queue;
    std::atomic<int> executed{0};
    std::vector<std::future<void>> callers;
    for (int i = 0; i < 3; ++i)
    {
        callers.push_back(std::async(std::launch::async, [&]()
        {
            queue.invoke([&]()
            {
                std::this_thread::sleep_for(5ms);
                ++executed;
                return nlohmann::json();
            }, 5000ms);
        }));
    }
    while (queue.pending() < 3)
    {
        std::this_thread::sleep_for(1ms);
    }

    CHECK(queue.drain(0ms) == 1);
    CHECK(executed.load() == 1);
    while (queue.pending() > 0)
    {
        queue.drain(100ms);
    }
    for (auto& caller : callers)
    {
        caller.get();
    }
    CHECK(executed.load() == 3);
}
