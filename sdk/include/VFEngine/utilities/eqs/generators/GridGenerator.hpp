#pragma once

#include "../IEQSGenerator.hpp"
#include "../EQSContextPoint.hpp"

namespace eqs
{
    // A square grid of points centered on the querier or the target, at its height.
    class GridGenerator : public IEQSGenerator
    {
    public:
        struct Settings
        {
            EQSContextPoint center = EQSContextPoint::Querier;
            int size = 11;         // points per side
            float spacing = 2.0f;  // metres between neighbouring points
        };

        explicit GridGenerator(const Settings& settings);

        bool generate(const EQSContext& context, const EQSProviderRefs& providers,
                      std::vector<EQSCandidate>& out, std::string& error) const override;

        std::string getName() const override { return "Grid"; }

    private:
        Settings settings;
    };
}
