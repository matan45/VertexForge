#include "RingGenerator.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace eqs
{
    RingGenerator::RingGenerator(float radius, int pointCount,
                                  int numRings, float ringSpacing)
        : radius(radius)
        , pointCount(pointCount)
        , numRings(numRings)
        , ringSpacing(ringSpacing)
    {
    }

    std::vector<EQSCandidate> RingGenerator::generate(const EQSContext& context,
                                                       const EQSProviderRefs& providers)
    {
        std::vector<EQSCandidate> candidates;
        candidates.reserve(static_cast<size_t>(pointCount) * numRings);

        float angleStep = glm::two_pi<float>() / static_cast<float>(pointCount);

        for (int ring = 0; ring < numRings; ++ring)
        {
            float currentRadius = radius + static_cast<float>(ring) * ringSpacing;

            for (int i = 0; i < pointCount; ++i)
            {
                float angle = static_cast<float>(i) * angleStep;

                EQSCandidate candidate;
                candidate.position = context.querierPosition + glm::vec3(
                    std::cos(angle) * currentRadius,
                    0.0f,
                    std::sin(angle) * currentRadius
                );
                candidate.totalScore = 0.0f;
                candidate.filtered = false;
                candidates.push_back(candidate);
            }
        }

        return candidates;
    }
}
