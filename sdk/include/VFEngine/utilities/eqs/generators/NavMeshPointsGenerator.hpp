#pragma once

#include "../IEQSGenerator.hpp"

namespace eqs
{
    class NavMeshPointsGenerator : public IEQSGenerator
    {
    public:
        NavMeshPointsGenerator(float radius = 20.0f, int pointCount = 20);

        std::vector<EQSCandidate> generate(const EQSContext& context,
                                            const EQSProviderRefs& providers) override;

        std::string getName() const override { return "NavMeshPointsGenerator"; }

    private:
        float radius;
        int pointCount;
    };
}
