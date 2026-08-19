#pragma once

#include <future>
#include <mutex>
#include <utility>

namespace streaming
{
    // VK-1592: bridges "somebody else owns the worker thread" to "AsyncLoadQueue owns a
    // std::future". A ResourceLoadScheduler LoadRequest carries an executeLoad lambda and the
    // scheduler decides when (and whether) it runs, so the producer cannot simply hand back a
    // std::async future the way the old bespoke sector IO did.
    //
    // Two hazards this exists to close, both of which end in a hung main thread or a throw
    // inside AsyncLoadQueue::poll()/drain(), which call future.get() unconditionally:
    //
    //  1. Two producers racing. A main-thread cancellation and a worker finishing can land at
    //     the same instant; a second promise::set_value throws future_error. fulfil() is
    //     idempotent and thread-safe, so whoever gets there first wins and the other is a no-op.
    //
    //  2. A request that never runs at all. The scheduler drops requests without executing them
    //     (queue-full rejection, priority eviction, the cancelled-pending sweep in update()).
    //     That destroys the lambda and with it the last reference to this slot, so the destructor
    //     resolves the promise with a default-constructed Result rather than leaving a broken
    //     promise behind. OWNERSHIP RULE: the only strong reference must live inside the
    //     executeLoad lambda - holders that outlive the request (the streamer's coord -> request
    //     map) must keep a weak_ptr, or a dropped request never resolves and drain() blocks
    //     forever.
    //
    // Result must be default-constructible, and its default must read as "nothing happened".
    template <typename Result>
    class AsyncResultSlot
    {
    public:
        AsyncResultSlot() = default;
        AsyncResultSlot(const AsyncResultSlot&) = delete;
        AsyncResultSlot& operator=(const AsyncResultSlot&) = delete;
        AsyncResultSlot(AsyncResultSlot&&) = delete;
        AsyncResultSlot& operator=(AsyncResultSlot&&) = delete;

        ~AsyncResultSlot() { fulfil(Result{}); }

        // Call once, before the slot is handed to any producer (get_future() throws on a
        // second call).
        [[nodiscard]] std::future<Result> getFuture() { return promise.get_future(); }

        // Deliver the result. Safe to call from any thread and any number of times; only the
        // first call has an effect. noexcept because the destructor calls it.
        void fulfil(Result value) noexcept
        {
            try
            {
                std::call_once(once, [&] { promise.set_value(std::move(value)); });
            }
            catch (...)
            {
                // set_value can only fail here if the shared state is already gone; there is
                // nothing left to report to and the caller may be a destructor.
            }
        }

    private:
        std::promise<Result> promise;
        std::once_flag once;
    };

} // namespace streaming
