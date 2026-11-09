#pragma once

#include "EQSQuery.hpp"
#include "EQSTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace eqs
{
    // Runs submitted queries in the EQS frame task (VK-1660; replaces the std::async EQSQueryEngine, whose
    // worker threads raced the physics step): whole queries, first in first out, until the frame's time budget
    // is spent. Results are deterministic and arrive one frame after submission at the earliest. Thread-safe:
    // submit / getResult / release come from the behavior-tree task, scripts and the main thread, update from
    // the EQS task (queries run outside the lock).
    class EQSScheduler
    {
    public:
        // Monotonic milliseconds; default std::chrono::steady_clock. Tests inject a fake.
        using Clock = std::function<double()>;

        static constexpr size_t kMaxPending = 256;
        // A finished result is dropped this many update() calls after it finished, unless released before.
        static constexpr uint64_t kResultLifetimeUpdates = 600;

        explicit EQSScheduler(Clock clock = {});

        EQSScheduler(const EQSScheduler&) = delete;
        EQSScheduler& operator=(const EQSScheduler&) = delete;

        // Invalid handle when `def` is null or kMaxPending queries are already waiting.
        EQSQueryHandle submit(std::shared_ptr<const EQSQueryDef> def, const EQSContext& context);

        // Runs pending queries in submission order (eqs::runQuery, never split) while the time spent in this
        // call stays under budgetMs - always at least one when any is pending. Then ages finished results.
        void update(const EQSProviderRefs& providers, float budgetMs);

        // Pending while queued, Running while update() runs it, Completed / Failed once done. An unknown,
        // released or expired handle reads as Failed with an error.
        EQSResult getResult(EQSQueryHandle handle) const;

        // Cancels a queued query, discards the one running, or drops a finished result. Unknown handles are
        // ignored.
        void release(EQSQueryHandle handle);
        void releaseAll();

        size_t pendingCount() const;
        size_t resultCount() const;

    private:
        struct PendingQuery
        {
            uint64_t id = 0;
            std::shared_ptr<const EQSQueryDef> def;
            EQSContext context;
        };

        struct FinishedQuery
        {
            EQSResult result;
            uint64_t finishedAtUpdate = 0; // updateCount when it finished
        };

        Clock clock;
        mutable std::mutex mutex;
        std::deque<PendingQuery> pending;
        uint64_t runningId = 0;         // the query update() is running now (0 = none)
        bool runningReleased = false;   // released while running: its result is discarded
        std::unordered_map<uint64_t, FinishedQuery> finished;
        uint64_t nextId = 1;
        uint64_t updateCount = 0;
    };
}
