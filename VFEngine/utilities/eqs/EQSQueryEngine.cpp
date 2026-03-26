#include "EQSQueryEngine.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace eqs
{
    EQSQueryEngine::~EQSQueryEngine()
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& [id, pending] : queries)
        {
            if (pending.future.valid())
            {
                pending.future.wait();
            }
        }
        queries.clear();
    }

    EQSQueryHandle EQSQueryEngine::submitQuery(const EQSQueryDef& queryDef,
                                                const EQSContext& context,
                                                const EQSProviderRefs& providers)
    {
        EQSQueryHandle handle;
        handle.id = nextId.fetch_add(1);

        auto future = std::async(std::launch::async,
            [this, queryDef, context, providers]()
            {
                return executeQuery(queryDef, context, providers);
            });

        std::lock_guard<std::mutex> lock(mutex);
        PendingQuery pending;
        pending.future = std::move(future);
        pending.result.status = EQSQueryStatus::Running;
        queries[handle.id] = std::move(pending);

        return handle;
    }

    EQSResult EQSQueryEngine::getResult(const EQSQueryHandle& handle) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = queries.find(handle.id);
        if (it == queries.end())
        {
            EQSResult result;
            result.status = EQSQueryStatus::Failed;
            return result;
        }
        return it->second.result;
    }

    void EQSQueryEngine::cancelQuery(const EQSQueryHandle& handle)
    {
        std::lock_guard<std::mutex> lock(mutex);
        queries.erase(handle.id);
    }

    void EQSQueryEngine::update()
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto& [id, pending] : queries)
        {
            if (pending.future.valid() &&
                pending.future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            {
                pending.result = pending.future.get();
            }
        }
    }

    EQSResult EQSQueryEngine::executeQuery(const EQSQueryDef& queryDef,
                                            const EQSContext& context,
                                            const EQSProviderRefs& providers)
    {
        EQSResult result;

        if (!queryDef.generator)
        {
            result.status = EQSQueryStatus::Failed;
            return result;
        }

        // Step 1: Generate candidates
        result.candidates = queryDef.generator->generate(context, providers);

        if (result.candidates.empty())
        {
            result.status = EQSQueryStatus::Completed;
            return result;
        }

        // Initialize scores
        for (auto& candidate : result.candidates)
        {
            candidate.totalScore = 1.0f;
            candidate.filtered = false;
        }

        // Step 2: Run filter tests first
        for (const auto& entry : queryDef.tests)
        {
            if (!entry.config.isFilter || !entry.test)
                continue;

            entry.test->runTest(result.candidates, context, providers, entry.config);

            // Remove filtered candidates
            result.candidates.erase(
                std::remove_if(result.candidates.begin(), result.candidates.end(),
                    [](const EQSCandidate& c) { return c.filtered; }),
                result.candidates.end());

            if (result.candidates.empty())
            {
                result.status = EQSQueryStatus::Completed;
                return result;
            }
        }

        // Step 3: Run scoring tests
        for (const auto& entry : queryDef.tests)
        {
            if (entry.config.isFilter || !entry.test)
                continue;

            // Compute raw scores by running the test
            // Tests store raw scores in totalScore temporarily
            std::vector<float> previousScores(result.candidates.size());
            for (size_t i = 0; i < result.candidates.size(); ++i)
            {
                previousScores[i] = result.candidates[i].totalScore;
                result.candidates[i].totalScore = 0.0f;
            }

            entry.test->runTest(result.candidates, context, providers, entry.config);

            // Collect raw scores and find min/max for normalization
            std::vector<float> rawScores(result.candidates.size());
            float minScore = std::numeric_limits<float>::max();
            float maxScore = std::numeric_limits<float>::lowest();

            for (size_t i = 0; i < result.candidates.size(); ++i)
            {
                rawScores[i] = result.candidates[i].totalScore;
                minScore = std::min(minScore, rawScores[i]);
                maxScore = std::max(maxScore, rawScores[i]);
            }

            // Normalize to [0,1] and apply scoring mode + weight + combine
            float range = maxScore - minScore;
            for (size_t i = 0; i < result.candidates.size(); ++i)
            {
                float normalized = (range > 0.0001f)
                    ? (rawScores[i] - minScore) / range
                    : 1.0f;

                // Apply scoring mode
                float scored = 0.0f;
                switch (entry.config.scoringMode)
                {
                case EQSScoringMode::Linear:
                    scored = normalized;
                    break;
                case EQSScoringMode::InverseLinear:
                    scored = 1.0f - normalized;
                    break;
                case EQSScoringMode::Square:
                    scored = normalized * normalized;
                    break;
                case EQSScoringMode::Constant:
                    scored = 1.0f;
                    break;
                }

                // Apply weight
                scored *= entry.config.weight;

                // Combine with previous score
                switch (entry.config.scoreCombine)
                {
                case EQSScoreCombine::Multiply:
                    result.candidates[i].totalScore = previousScores[i] * scored;
                    break;
                case EQSScoreCombine::Additive:
                    result.candidates[i].totalScore = previousScores[i] + scored;
                    break;
                }
            }
        }

        // Step 4: Sort by totalScore descending
        std::sort(result.candidates.begin(), result.candidates.end(),
            [](const EQSCandidate& a, const EQSCandidate& b)
            {
                return a.totalScore > b.totalScore;
            });

        // Step 5: Trim to maxResults
        if (queryDef.maxResults > 0 && result.candidates.size() > queryDef.maxResults)
        {
            result.candidates.resize(queryDef.maxResults);
        }

        result.status = EQSQueryStatus::Completed;
        return result;
    }
}
