#pragma once

#include <span>
#include <string>
#include "EQSTypes.hpp"

namespace eqs
{
    // Measures candidates. A test only produces a metric per candidate; filtering, normalisation and
    // scoring are the runner's (EQSQueryRunner, from the entry's EQSTestConfig). Immutable after
    // construction (shared by every run of a template, on any thread).
    class IEQSTest
    {
    public:
        virtual ~IEQSTest() = default;

        // Writes metrics[i] for candidates[i] (the spans have the same size); kNoMetric for a candidate it
        // cannot measure. Returns false with `error` set when the query cannot run - a provider it needs is
        // unset, or it is measured from a target the context lacks.
        virtual bool evaluate(std::span<const EQSCandidate> candidates, const EQSContext& context,
                              const EQSProviderRefs& providers, std::span<float> metrics,
                              std::string& error) const = 0;

        virtual std::string getName() const = 0;
    };
}
