#include "GridGenerator.hpp"

namespace eqs
{
    GridGenerator::GridGenerator(int gridSize, float spacing)
        : gridSize(gridSize)
        , spacing(spacing)
    {
    }

    std::vector<EQSCandidate> GridGenerator::generate(const EQSContext& context,
                                                       const EQSProviderRefs& providers)
    {
        std::vector<EQSCandidate> candidates;
        candidates.reserve(static_cast<size_t>(gridSize) * gridSize);

        float halfExtent = (static_cast<float>(gridSize - 1) * spacing) * 0.5f;

        for (int z = 0; z < gridSize; ++z)
        {
            for (int x = 0; x < gridSize; ++x)
            {
                EQSCandidate candidate;
                candidate.position = context.querierPosition + glm::vec3(
                    static_cast<float>(x) * spacing - halfExtent,
                    0.0f,
                    static_cast<float>(z) * spacing - halfExtent
                );
                candidate.totalScore = 0.0f;
                candidate.filtered = false;
                candidates.push_back(candidate);
            }
        }

        return candidates;
    }
}
