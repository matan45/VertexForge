#pragma once

#include "../IEQSTest.hpp"
#include "../EQSContextPoint.hpp"

namespace eqs
{
    // Metric: the navmesh path length (metres) from the querier or the target to the candidate, for an agent of
    // the context's size; kNoMetric when there is no path.
    class PathfindingCostTest : public IEQSTest
    {
    public:
        struct Settings
        {
            EQSContextPoint from = EQSContextPoint::Querier;
        };

        explicit PathfindingCostTest(const Settings& settings);

        bool evaluate(std::span<const EQSCandidate> candidates, const EQSContext& context,
                      const EQSProviderRefs& providers, std::span<float> metrics,
                      std::string& error) const override;

        std::string getName() const override { return "PathCost"; }

    private:
        Settings settings;
    };
}
