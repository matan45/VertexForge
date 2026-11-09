#include "EQSQueryRunner.hpp"
#include "EQSQuerySpec.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <format>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

namespace eqs
{
    namespace
    {
        // Metrics closer than this (relative to their magnitude) count as equal: the test then tells the
        // candidates apart in no way.
        constexpr float kEqualMetricTolerance = 1.0e-6f;

        float shapeScore(EQSScoringMode mode, float normalized)
        {
            switch (mode)
            {
            case EQSScoringMode::Linear: return normalized;
            case EQSScoringMode::InverseLinear: return 1.0f - normalized;
            case EQSScoringMode::Square: return normalized * normalized;
            case EQSScoringMode::Constant: return 1.0f;
            }
            return normalized;
        }

        bool passesFilter(const EQSTestConfig& config, float metric)
        {
            if (std::isnan(metric))
                return false;
            if (config.filterMin && metric < *config.filterMin)
                return false;
            if (config.filterMax && metric > *config.filterMax)
                return false;
            return true;
        }

        // The query's stages; false with `error` set when it cannot run. `detail` (optional) has one entry per
        // generated candidate.
        bool runStages(const EQSQueryDef& def, const EQSContext& context, const EQSProviderRefs& providers,
                       std::vector<EQSCandidate>& candidates, std::vector<EQSDebugCandidate>* detail,
                       std::string& error)
        {
            if (!def.generator)
            {
                error = "the query has no generator";
                return false;
            }
            for (size_t t = 0; t < def.tests.size(); ++t)
            {
                if (!def.tests[t].test)
                {
                    error = std::format("test {} of the query has no implementation", t);
                    return false;
                }
            }

            if (!def.generator->generate(context, providers, candidates, error))
            {
                if (error.empty())
                    error = "the " + def.generator->getName() + " generator failed";
                return false;
            }
            if (candidates.size() > kMaxCandidates)
            {
                error = std::format("the {} generator produced {} candidates; the limit is {}",
                                    def.generator->getName(), candidates.size(), kMaxCandidates);
                return false;
            }

            const size_t testCount = def.tests.size();
            for (EQSCandidate& candidate : candidates)
            {
                candidate.totalScore = 1.0f;
                candidate.filtered = false;
            }
            if (detail)
            {
                detail->resize(candidates.size());
                for (size_t i = 0; i < candidates.size(); ++i)
                {
                    EQSDebugCandidate& entry = (*detail)[i];
                    entry.position = candidates[i].position;
                    entry.entityId = candidates[i].entityId;
                    entry.filteredByTest = -1;
                    entry.metrics.assign(testCount, kNoMetric);
                    entry.scores.assign(testCount, 0.0f);
                    entry.total = 0.0f;
                }
            }

            // Indices of the candidates still in the running, in generation order.
            std::vector<size_t> alive(candidates.size());
            std::iota(alive.begin(), alive.end(), size_t{0});
            std::vector<EQSCandidate> subset;
            std::vector<float> metrics;

            // Measures every surviving candidate with test t into `metrics` (parallel to `alive`).
            auto measure = [&](size_t t) -> bool
            {
                subset.clear();
                subset.reserve(alive.size());
                for (size_t index : alive)
                    subset.push_back(candidates[index]);
                metrics.assign(subset.size(), kNoMetric);

                std::string testError;
                if (!def.tests[t].test->evaluate(subset, context, providers, metrics, testError))
                {
                    error = testError.empty() ? "the " + def.tests[t].test->getName() + " test failed" : testError;
                    return false;
                }
                if (detail)
                {
                    for (size_t k = 0; k < alive.size(); ++k)
                        (*detail)[alive[k]].metrics[t] = metrics[k];
                }
                return true;
            };

            // 1. Every filter, in declared order.
            for (size_t t = 0; t < testCount && !alive.empty(); ++t)
            {
                const EQSTestConfig& config = def.tests[t].config;
                if (config.purpose != EQSTestPurpose::Filter)
                    continue;
                if (!measure(t))
                    return false;

                std::vector<size_t> kept;
                kept.reserve(alive.size());
                for (size_t k = 0; k < alive.size(); ++k)
                {
                    const size_t index = alive[k];
                    if (passesFilter(config, metrics[k]))
                    {
                        kept.push_back(index);
                        continue;
                    }
                    candidates[index].filtered = true;
                    if (detail)
                        (*detail)[index].filteredByTest = static_cast<int>(t);
                }
                alive = std::move(kept);
            }

            // 2. Every scorer, in declared order.
            for (size_t t = 0; t < testCount && !alive.empty(); ++t)
            {
                const EQSTestConfig& config = def.tests[t].config;
                if (config.purpose != EQSTestPurpose::Score)
                    continue;
                if (!measure(t))
                    return false;

                float low = std::numeric_limits<float>::infinity();
                float high = -std::numeric_limits<float>::infinity();
                for (float metric : metrics)
                {
                    if (std::isnan(metric))
                        continue;
                    low = std::min(low, metric);
                    high = std::max(high, metric);
                }
                const float range = high - low;
                const float magnitude = std::max(1.0f, std::max(std::abs(low), std::abs(high)));
                // When every measured candidate has the same metric the test cannot rank them: it scores 1
                // in every mode (InverseLinear would otherwise zero a Multiply chain and erase the other
                // tests' ranking).
                const bool discriminating = low <= high && range > kEqualMetricTolerance * magnitude;

                for (size_t k = 0; k < alive.size(); ++k)
                {
                    const float metric = metrics[k];
                    float score = 0.0f; // no metric: the worst score
                    if (!std::isnan(metric))
                    {
                        const float shaped = discriminating ? shapeScore(config.mode, (metric - low) / range) : 1.0f;
                        score = shaped * config.weight;
                    }

                    EQSCandidate& candidate = candidates[alive[k]];
                    if (config.combine == EQSScoreCombine::Multiply)
                        candidate.totalScore *= score;
                    else
                        candidate.totalScore += score;
                    if (detail)
                        (*detail)[alive[k]].scores[t] = score;
                }
            }

            // 3. The survivors, best first (stable: ties keep generation order), trimmed to maxResults.
            std::vector<EQSCandidate> survivors;
            survivors.reserve(alive.size());
            for (size_t index : alive)
            {
                survivors.push_back(candidates[index]);
                if (detail)
                    (*detail)[index].total = candidates[index].totalScore;
            }
            std::stable_sort(survivors.begin(), survivors.end(), [](const EQSCandidate& a, const EQSCandidate& b)
            {
                return a.totalScore > b.totalScore;
            });
            if (def.maxResults > 0 && survivors.size() > def.maxResults)
                survivors.resize(def.maxResults);
            candidates = std::move(survivors);
            return true;
        }
    }

    EQSResult runQuery(const EQSQueryDef& def, const EQSContext& context, const EQSProviderRefs& providers,
                       EQSDebugResult* debug)
    {
        const auto started = std::chrono::steady_clock::now();

        EQSResult result;
        std::vector<EQSCandidate> candidates;
        std::vector<EQSDebugCandidate> detail;
        std::string error;
        bool succeeded = false;
        try
        {
            succeeded = runStages(def, context, providers, candidates, debug ? &detail : nullptr, error);
        }
        catch (const std::exception& exception)
        {
            succeeded = false;
            error = std::format("the query threw: {}", exception.what());
        }
        catch (...)
        {
            succeeded = false;
            error = "the query threw an unknown exception";
        }

        if (succeeded)
        {
            result.status = EQSQueryStatus::Completed;
            result.candidates = std::move(candidates);
        }
        else
        {
            result.status = EQSQueryStatus::Failed;
            result.error = error.empty() ? std::string("the query failed") : error;
        }

        if (debug)
        {
            debug->result = result;
            debug->testNames.clear();
            debug->testNames.reserve(def.tests.size());
            for (const EQSTestEntry& entry : def.tests)
                debug->testNames.push_back(entry.test ? entry.test->getName() : std::string());
            debug->candidates = std::move(detail);
            debug->elapsedMs =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        }
        return result;
    }
}
