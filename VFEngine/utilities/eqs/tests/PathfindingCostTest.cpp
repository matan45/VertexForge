#include "PathfindingCostTest.hpp"
#include <glm/glm.hpp>

namespace eqs
{
    void PathfindingCostTest::runTest(std::vector<EQSCandidate>& candidates,
                                      const EQSContext& context,
                                      const EQSProviderRefs& providers,
                                      const EQSTestConfig& config)
    {
        if (!providers.pathfindingCost)
            return;

        for (auto& candidate : candidates)
        {
            if (candidate.filtered)
                continue;

            float cost = providers.pathfindingCost(context.querierPosition,
                                                    candidate.position);

            if (cost < 0.0f)
            {
                // No valid path found
                if (config.isFilter)
                {
                    candidate.filtered = true;
                }
                else
                {
                    candidate.totalScore = 0.0f;
                }
            }
            else
            {
                candidate.totalScore = cost;

                if (config.isFilter && cost > config.filterThreshold)
                {
                    candidate.filtered = true;
                }
            }
        }
    }
}
