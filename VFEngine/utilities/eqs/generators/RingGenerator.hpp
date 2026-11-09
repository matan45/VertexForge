#pragma once

#include "../IEQSGenerator.hpp"
#include "../EQSContextPoint.hpp"

namespace eqs
{
    // Concentric rings of evenly spaced points around the querier or the target, at its height.
    class RingGenerator : public IEQSGenerator
    {
    public:
        struct Settings
        {
            EQSContextPoint center = EQSContextPoint::Querier;
            float radius = 10.0f;      // the innermost ring
            int pointCount = 12;       // points per ring
            int ringCount = 1;
            float ringSpacing = 5.0f;  // radius added per further ring
        };

        explicit RingGenerator(const Settings& settings);

        bool generate(const EQSContext& context, const EQSProviderRefs& providers,
                      std::vector<EQSCandidate>& out, std::string& error) const override;

        std::string getName() const override { return "Ring"; }

    private:
        Settings settings;
    };
}
