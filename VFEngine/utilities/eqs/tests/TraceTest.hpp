#pragma once

#include "../IEQSTest.hpp"
#include "../EQSContextPoint.hpp"

namespace eqs
{
    // Metric: 1 when the segment from the querier's (or the target's) eye to the candidate is clear, else 0.
    // The colliders of the looking entity and of the candidate's entity never block it.
    class TraceTest : public IEQSTest
    {
    public:
        struct Settings
        {
            EQSContextPoint from = EQSContextPoint::Querier;
            float eyeHeight = 1.6f;       // above the looking point
            float candidateHeight = 1.0f; // above the candidate
        };

        explicit TraceTest(const Settings& settings);

        bool evaluate(std::span<const EQSCandidate> candidates, const EQSContext& context,
                      const EQSProviderRefs& providers, std::span<float> metrics,
                      std::string& error) const override;

        std::string getName() const override { return "Trace"; }

    private:
        Settings settings;
    };
}
