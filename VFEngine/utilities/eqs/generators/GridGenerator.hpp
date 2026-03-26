#pragma once

#include "../IEQSGenerator.hpp"

namespace eqs
{
    class GridGenerator : public IEQSGenerator
    {
    public:
        GridGenerator(int gridSize = 11, float spacing = 2.0f);

        std::vector<EQSCandidate> generate(const EQSContext& context,
                                            const EQSProviderRefs& providers) override;

        std::string getName() const override { return "GridGenerator"; }

    private:
        int gridSize;
        float spacing;
    };
}
