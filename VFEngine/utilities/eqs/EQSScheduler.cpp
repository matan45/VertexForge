#include "EQSScheduler.hpp"
#include "EQSQueryRunner.hpp"
#include <algorithm>
#include <chrono>
#include <exception>
#include <format>
#include <utility>

namespace eqs
{
    namespace
    {
        double steadyMilliseconds()
        {
            return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        EQSResult failedResult(std::string error)
        {
            EQSResult result;
            result.status = EQSQueryStatus::Failed;
            result.error = std::move(error);
            return result;
        }
    }

    EQSScheduler::EQSScheduler(Clock clock)
        : clock(clock ? std::move(clock) : Clock(&steadyMilliseconds))
    {
    }

    EQSQueryHandle EQSScheduler::submit(std::shared_ptr<const EQSQueryDef> def, const EQSContext& context)
    {
        if (!def)
            return {};

        std::lock_guard lock(mutex);
        if (pending.size() >= kMaxPending)
            return {};

        PendingQuery query;
        query.id = nextId++;
        query.def = std::move(def);
        query.context = context;
        pending.push_back(std::move(query));
        return EQSQueryHandle{pending.back().id};
    }

    void EQSScheduler::update(const EQSProviderRefs& providers, float budgetMs)
    {
        const double started = clock();
        bool ranAny = false;
        while (true)
        {
            PendingQuery query;
            {
                std::lock_guard lock(mutex);
                if (pending.empty())
                    break;
                // Whole queries only: the first one always runs, later ones while the budget lasts.
                if (ranAny && clock() - started >= static_cast<double>(budgetMs))
                    break;
                query = std::move(pending.front());
                pending.pop_front();
                runningId = query.id;
                runningReleased = false;
            }

            // Outside the lock, so submit / getResult / release from other threads never wait for a query.
            EQSResult result;
            try
            {
                result = runQuery(*query.def, query.context, providers);
            }
            catch (const std::exception& exception)
            {
                result = failedResult(std::format("the query threw: {}", exception.what()));
            }
            catch (...)
            {
                result = failedResult("the query threw an unknown exception");
            }

            {
                std::lock_guard lock(mutex);
                if (!runningReleased)
                    finished[query.id] = FinishedQuery{std::move(result), updateCount};
                runningId = 0;
                runningReleased = false;
            }
            ranAny = true;
        }

        std::lock_guard lock(mutex);
        ++updateCount;
        for (auto it = finished.begin(); it != finished.end();)
        {
            // A result that finished during update N is dropped at the end of update N + kResultLifetimeUpdates.
            if (updateCount - it->second.finishedAtUpdate > kResultLifetimeUpdates)
                it = finished.erase(it);
            else
                ++it;
        }
    }

    EQSResult EQSScheduler::getResult(EQSQueryHandle handle) const
    {
        std::lock_guard lock(mutex);
        if (handle.isValid())
        {
            if (handle.id == runningId && !runningReleased)
            {
                EQSResult running;
                running.status = EQSQueryStatus::Running;
                return running;
            }
            for (const PendingQuery& query : pending)
            {
                if (query.id == handle.id)
                {
                    EQSResult queued;
                    queued.status = EQSQueryStatus::Pending;
                    return queued;
                }
            }
            if (auto it = finished.find(handle.id); it != finished.end())
                return it->second.result;
        }
        return failedResult("unknown, released or expired EQS query");
    }

    void EQSScheduler::release(EQSQueryHandle handle)
    {
        if (!handle.isValid())
            return;

        std::lock_guard lock(mutex);
        if (handle.id == runningId)
        {
            runningReleased = true;
            return;
        }
        auto queued = std::find_if(pending.begin(), pending.end(),
                                   [&handle](const PendingQuery& query) { return query.id == handle.id; });
        if (queued != pending.end())
        {
            pending.erase(queued);
            return;
        }
        finished.erase(handle.id);
    }

    void EQSScheduler::releaseAll()
    {
        std::lock_guard lock(mutex);
        pending.clear();
        finished.clear();
        if (runningId != 0)
            runningReleased = true;
    }

    size_t EQSScheduler::pendingCount() const
    {
        std::lock_guard lock(mutex);
        return pending.size();
    }

    size_t EQSScheduler::resultCount() const
    {
        std::lock_guard lock(mutex);
        return finished.size();
    }
}
