#pragma once

#include "../IEQSGenerator.hpp"

namespace eqs
{
    class RingGenerator : public IEQSGenerator
    {
    public:
        RingGenerator(float radius = 10.0f, int pointCount = 12,
                      int numRings = 1, float ringSpacing = 5.0f);

        std::vector<EQSCandidate> generate(const EQSContext& context,
                                            const EQSProviderRefs& providers) override;

        std::string getName() const override { return "RingGenerator"; }

    private:
        float radius;
        int pointCount;
        int numRings;
        float ringSpacing;
    };
}
