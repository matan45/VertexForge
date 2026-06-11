#pragma once

#include <functional>
#include <future>
#include <unordered_map>
#include <utility>
#include <vector>

namespace streaming
{
    // Keyed in-flight async load tracking with cancellation — the hand-rolled
    // map-of-{future, cancelled} pattern shared by sector and terrain streaming.
    // Single-threaded by design: launch/poll/cancel from the owning update loop.
    template <typename Key, typename Result, typename Hash = std::hash<Key>>
    class AsyncLoadQueue
    {
    public:
        [[nodiscard]] bool contains(const Key& key) const { return pending.contains(key); }
        [[nodiscard]] size_t size() const { return pending.size(); }
        [[nodiscard]] bool empty() const { return pending.empty(); }

        // Track an in-flight load. Returns false (and drops nothing) if a load
        // for this key is already pending.
        bool launch(const Key& key, std::future<Result> future)
        {
            if (pending.contains(key))
                return false;
            pending.emplace(key, Entry{std::move(future), false});
            return true;
        }

        // Mark a pending load as cancelled; its result is discarded on poll.
        // (std::async has no cooperative cancellation — the task still runs.)
        void cancel(const Key& key)
        {
            auto it = pending.find(key);
            if (it != pending.end())
                it->second.cancelled = true;
        }

        // Invoke onReady(key, result) for every completed, non-cancelled load and
        // remove completed entries. Safe to call every frame.
        template <typename Fn>
        void poll(Fn&& onReady)
        {
            auto it = pending.begin();
            while (it != pending.end())
            {
                if (it->second.future.wait_for(std::chrono::seconds(0)) !=
                    std::future_status::ready)
                {
                    ++it;
                    continue;
                }

                Result result = it->second.future.get();
                Key key = it->first;
                bool cancelled = it->second.cancelled;
                it = pending.erase(it);

                if (!cancelled)
                    onReady(key, std::move(result));
            }
        }

        // Block until every in-flight load completes, discarding all results.
        void drain()
        {
            for (auto& [key, entry] : pending)
            {
                if (entry.future.valid())
                    entry.future.get();
            }
            pending.clear();
        }

    private:
        struct Entry
        {
            std::future<Result> future;
            bool cancelled = false;
        };

        std::unordered_map<Key, Entry, Hash> pending;
    };

} // namespace streaming
