#include "OnNavMeshTest.hpp"

namespace eqs
{
    OnNavMeshTest::OnNavMeshTest(const Settings& settings)
        : settings(settings)
    {
    }

    bool OnNavMeshTest::evaluate(std::span<const EQSCandidate> candidates, const EQSContext& /*context*/,
                                 const EQSProviderRefs& providers, std::span<float> metrics,
                                 std::string& error) const
    {
        if (!providers.isPointOnNavmesh)
        {
            error = "the OnNavMesh test needs the navmesh (isPointOnNavmesh is not available)";
            return false;
        }

        for (size_t i = 0; i < candidates.size(); ++i)
            metrics[i] = providers.isPointOnNavmesh(candidates[i].position, settings.tolerance) ? 1.0f : 0.0f;
        return true;
    }
}
