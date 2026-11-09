#include "PathfindingCostTest.hpp"

namespace eqs
{
    PathfindingCostTest::PathfindingCostTest(const Settings& settings)
        : settings(settings)
    {
    }

    bool PathfindingCostTest::evaluate(std::span<const EQSCandidate> candidates, const EQSContext& context,
                                       const EQSProviderRefs& providers, std::span<float> metrics,
                                       std::string& error) const
    {
        if (!providers.pathLength)
        {
            error = "the PathCost test needs the navmesh (pathLength is not available)";
            return false;
        }
        glm::vec3 from{0.0f};
        if (!resolveContextPoint(context, settings.from, from, error, "the PathCost test"))
            return false;

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            const float length = providers.pathLength(from, candidates[i].position, context.agentRadius,
                                                      context.agentHeight);
            metrics[i] = length < 0.0f ? kNoMetric : length;
        }
        return true;
    }
}
