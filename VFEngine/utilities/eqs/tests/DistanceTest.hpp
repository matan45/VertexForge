#pragma once

#include "../IEQSTest.hpp"
#include "../EQSContextPoint.hpp"

namespace eqs
{
    // Metric: metres from the candidate to the querier or the target (XZ when `horizontal`).
    class DistanceTest : public IEQSTest
    {
    public:
        struct Settings
        {
            EQSContextPoint to = EQSContextPoint::Querier;
            bool horizontal = true; // ignore height, like the behavior tree's TargetDistance service
        };

        explicit DistanceTest(const Settings& settings);

        bool evaluate(std::span<const EQSCandidate> candidates, const EQSContext& context,
                      const EQSProviderRefs& providers, std::span<float> metrics,
                      std::string& error) const override;

        std::string getName() const override { return "Distance"; }

    private:
        Settings settings;
    };
}
