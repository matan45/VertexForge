#pragma once

#include "../IEQSTest.hpp"

namespace eqs
{
    class TraceTest : public IEQSTest
    {
    public:
        // maxDistance: maximum ray distance for visibility check
        explicit TraceTest(float maxDistance = 100.0f);

        void runTest(std::vector<EQSCandidate>& candidates,
                     const EQSContext& context,
                     const EQSProviderRefs& providers,
                     const EQSTestConfig& config) override;

        std::string getName() const override { return "TraceTest"; }

    private:
        float maxDistance;
    };
}
