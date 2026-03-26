#include "DotProductTest.hpp"
#include <glm/glm.hpp>

namespace eqs
{
    void DotProductTest::runTest(std::vector<EQSCandidate>& candidates,
                                 const EQSContext& context,
                                 const EQSProviderRefs& providers,
                                 const EQSTestConfig& config)
    {
        glm::vec3 forward = glm::normalize(context.querierForward);

        for (auto& candidate : candidates)
        {
            if (candidate.filtered)
                continue;

            glm::vec3 toCandidate = candidate.position - context.querierPosition;
            float length = glm::length(toCandidate);

            if (length < 0.0001f)
            {
                candidate.totalScore = 0.5f;
                continue;
            }

            glm::vec3 direction = toCandidate / length;
            float dot = glm::dot(forward, direction);

            // Remap from [-1,1] to [0,1]
            candidate.totalScore = (dot + 1.0f) * 0.5f;

            if (config.isFilter && candidate.totalScore < config.filterThreshold)
            {
                candidate.filtered = true;
            }
        }
    }
}
