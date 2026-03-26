#include "DistanceTest.hpp"
#include <glm/glm.hpp>

namespace eqs
{
    void DistanceTest::runTest(std::vector<EQSCandidate>& candidates,
                               const EQSContext& context,
                               const EQSProviderRefs& providers,
                               const EQSTestConfig& config)
    {
        glm::vec3 referencePoint = context.hasTarget
            ? context.targetPosition
            : context.querierPosition;

        for (auto& candidate : candidates)
        {
            if (candidate.filtered)
                continue;

            float distance = glm::distance(candidate.position, referencePoint);
            candidate.totalScore = distance;

            if (config.isFilter && distance > config.filterThreshold)
            {
                candidate.filtered = true;
            }
        }
    }
}
