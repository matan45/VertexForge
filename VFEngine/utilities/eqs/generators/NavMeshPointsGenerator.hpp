#pragma once

#include "../IEQSGenerator.hpp"
#include "../EQSContextPoint.hpp"

namespace eqs
{
    // Random points in a disc around the querier or the target, each moved onto the navmesh
    // (EQSProviderRefs::projectToNavmesh); points with no navmesh within snapRadius are dropped. The points
    // follow EQSContext::seed, so the same seed gives the same candidates.
    class NavMeshPointsGenerator : public IEQSGenerator
    {
    public:
        struct Settings
        {
            EQSContextPoint center = EQSContextPoint::Querier;
            float radius = 20.0f;
            int pointCount = 20;
            float snapRadius = 2.0f; // how far a sample may be from the navmesh
        };

        explicit NavMeshPointsGenerator(const Settings& settings);

        bool generate(const EQSContext& context, const EQSProviderRefs& providers,
                      std::vector<EQSCandidate>& out, std::string& error) const override;

        std::string getName() const override { return "NavMeshPoints"; }

    private:
        Settings settings;
    };
}
