#include "TraceTest.hpp"
#include <glm/glm.hpp>

namespace eqs
{
    TraceTest::TraceTest(float maxDistance)
        : maxDistance(maxDistance)
    {
    }

    void TraceTest::runTest(std::vector<EQSCandidate>& candidates,
                             const EQSContext& context,
                             const EQSProviderRefs& providers,
                             const EQSTestConfig& config)
    {
        if (!providers.raycast)
            return;

        glm::vec3 from = context.querierPosition;

        for (auto& candidate : candidates)
        {
            if (candidate.filtered)
                continue;

            float distance = glm::distance(from, candidate.position);
            bool occluded = providers.raycast(from, candidate.position,
                                              glm::min(distance, maxDistance));

            if (config.isFilter)
            {
                if (occluded)
                {
                    candidate.filtered = true;
                }
            }
            else
            {
                candidate.totalScore = occluded ? 0.0f : 1.0f;
            }
        }
    }
}
