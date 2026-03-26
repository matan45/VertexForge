#pragma once

#include <vector>
#include <string>
#include "EQSTypes.hpp"

namespace eqs
{
    class IEQSTest
    {
    public:
        virtual ~IEQSTest() = default;

        virtual void runTest(std::vector<EQSCandidate>& candidates,
                             const EQSContext& context,
                             const EQSProviderRefs& providers,
                             const EQSTestConfig& config) = 0;

        virtual std::string getName() const = 0;
    };
}
