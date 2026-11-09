#pragma once

#include <string>
#include <vector>
#include "EQSTypes.hpp"

namespace eqs
{
    // Produces a query's candidates. Immutable after construction (one instance is shared by every run of a
    // template, on any thread).
    class IEQSGenerator
    {
    public:
        virtual ~IEQSGenerator() = default;

        // Appends the candidates to `out`. Returns false with `error` set when the query cannot run - a
        // provider it needs is unset, or it is centered on a target the context lacks.
        virtual bool generate(const EQSContext& context, const EQSProviderRefs& providers,
                              std::vector<EQSCandidate>& out, std::string& error) const = 0;

        virtual std::string getName() const = 0;
    };
}
