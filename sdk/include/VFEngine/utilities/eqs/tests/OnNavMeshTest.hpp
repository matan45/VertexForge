#pragma once

#include "../IEQSTest.hpp"

namespace eqs
{
    // Metric: 1 when the candidate lies on the navmesh (within `tolerance`), else 0.
    class OnNavMeshTest : public IEQSTest
    {
    public:
        struct Settings
        {
            float tolerance = 2.0f;
        };

        explicit OnNavMeshTest(const Settings& settings);

        bool evaluate(std::span<const EQSCandidate> candidates, const EQSContext& context,
                      const EQSProviderRefs& providers, std::span<float> metrics,
                      std::string& error) const override;

        std::string getName() const override { return "OnNavMesh"; }

    private:
        Settings settings;
    };
}
