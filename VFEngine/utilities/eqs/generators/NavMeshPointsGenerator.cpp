#include "NavMeshPointsGenerator.hpp"
#include <glm/gtc/constants.hpp>
#include <random>
#include <cmath>

namespace eqs
{
    NavMeshPointsGenerator::NavMeshPointsGenerator(float radius, int pointCount)
        : radius(radius)
        , pointCount(pointCount)
    {
    }

    std::vector<EQSCandidate> NavMeshPointsGenerator::generate(const EQSContext& context,
                                                                const EQSProviderRefs& providers)
    {
        std::vector<EQSCandidate> candidates;
        candidates.reserve(static_cast<size_t>(pointCount));

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> distAngle(0.0f, glm::two_pi<float>());
        std::uniform_real_distribution<float> distRadius(0.0f, radius);

        for (int i = 0; i < pointCount; ++i)
        {
            float angle = distAngle(gen);
            // Use square root for uniform distribution within circle
            float r = std::sqrt(distRadius(gen) / radius) * radius;

            EQSCandidate candidate;
            candidate.position = context.querierPosition + glm::vec3(
                std::cos(angle) * r,
                0.0f,
                std::sin(angle) * r
            );
            candidate.totalScore = 0.0f;
            candidate.filtered = false;
            candidates.push_back(candidate);
        }

        return candidates;
    }
}
