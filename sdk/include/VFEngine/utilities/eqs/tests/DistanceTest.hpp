#pragma once

#include "../IEQSTest.hpp"

namespace eqs
{
    class DistanceTest : public IEQSTest
    {
    public:
        void runTest(std::vector<EQSCandidate>& candidates,
                     const EQSContext& context,
                     const EQSProviderRefs& providers,
                     const EQSTestConfig& config) override;

        std::string getName() const override { return "DistanceTest"; }
    };
}
