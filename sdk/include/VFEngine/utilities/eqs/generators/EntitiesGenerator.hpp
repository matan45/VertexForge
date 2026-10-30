#pragma once

#include "../IEQSGenerator.hpp"
#include "../EQSContextPoint.hpp"
#include <cstdint>
#include <string>

namespace eqs
{
    // The entities named `name` (or whose name starts with it) within `radius` of the querier or the target,
    // nearest first (EQSProviderRefs::findEntities). Never the querier itself. Each candidate carries the
    // entity and its world position.
    class EntitiesGenerator : public IEQSGenerator
    {
    public:
        struct Settings
        {
            EQSContextPoint center = EQSContextPoint::Querier;
            float radius = 25.0f;
            std::string name;
            bool prefix = false;
            bool includeInactive = false;
            int maxCount = 64;
        };

        explicit EntitiesGenerator(Settings settings);

        bool generate(const EQSContext& context, const EQSProviderRefs& providers,
                      std::vector<EQSCandidate>& out, std::string& error) const override;

        std::string getName() const override { return "Entities"; }

    private:
        Settings settings;
    };
}
