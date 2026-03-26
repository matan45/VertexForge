#include "OnNavMeshTest.hpp"

namespace eqs
{
    OnNavMeshTest::OnNavMeshTest(float tolerance)
        : tolerance(tolerance)
    {
    }

    void OnNavMeshTest::runTest(std::vector<EQSCandidate>& candidates,
                                 const EQSContext& context,
                                 const EQSProviderRefs& providers,
                                 const EQSTestConfig& config)
    {
        if (!providers.isPointOnNavmesh)
            return;

        for (auto& candidate : candidates)
        {
            if (candidate.filtered)
                continue;

            bool onNavmesh = providers.isPointOnNavmesh(candidate.position, tolerance);

            if (config.isFilter)
            {
                if (!onNavmesh)
                {
                    candidate.filtered = true;
                }
            }
            else
            {
                candidate.totalScore = onNavmesh ? 1.0f : 0.0f;
            }
        }
    }
}
