#pragma once

#include <vector>
#include <string>
#include "EQSTypes.hpp"

namespace eqs
{
    class IEQSGenerator
    {
    public:
        virtual ~IEQSGenerator() = default;

        virtual std::vector<EQSCandidate> generate(const EQSContext& context,
                                                    const EQSProviderRefs& providers) = 0;

        virtual std::string getName() const = 0;
    };
}
