#pragma once

#include "../IEQSTest.hpp"

namespace eqs
{
    class OnNavMeshTest : public IEQSTest
    {
    public:
        // Tolerance for navmesh point query (vertical search extent)
        explicit OnNavMeshTest(float tolerance = 2.0f);

        void runTest(std::vector<EQSCandidate>& candidates,
                     const EQSContext& context,
                     const EQSProviderRefs& providers,
                     const EQSTestConfig& config) override;

        std::string getName() const override { return "OnNavMeshTest"; }

    private:
        float tolerance;
    };
}
