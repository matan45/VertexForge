#pragma once

#include <unordered_map>
#include <future>
#include <mutex>
#include <atomic>
#include <cstdint>
#include "EQSTypes.hpp"
#include "EQSQuery.hpp"

namespace eqs
{
    class EQSQueryEngine
    {
    public:
        EQSQueryEngine() = default;
        ~EQSQueryEngine();

        EQSQueryEngine(const EQSQueryEngine&) = delete;
        EQSQueryEngine& operator=(const EQSQueryEngine&) = delete;

        EQSQueryHandle submitQuery(const EQSQueryDef& queryDef,
                                   const EQSContext& context,
                                   const EQSProviderRefs& providers);

        EQSResult getResult(const EQSQueryHandle& handle) const;

        void cancelQuery(const EQSQueryHandle& handle);

        void update();

    private:
        EQSResult executeQuery(const EQSQueryDef& queryDef,
                               const EQSContext& context,
                               const EQSProviderRefs& providers);

        void applyScoring(float rawScore, EQSCandidate& candidate,
                          const EQSTestConfig& config);

        struct PendingQuery
        {
            std::future<EQSResult> future;
            EQSResult result;
        };

        mutable std::mutex mutex;
        std::unordered_map<uint64_t, PendingQuery> queries;
        std::atomic<uint64_t> nextId{ 1 };
    };
}
