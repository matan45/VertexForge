#pragma once

#include "../IEQSTest.hpp"
#include <cstdint>

namespace eqs
{
    // Metric (0..1, on the XZ plane): how well the direction from the querier to the candidate lines up with
    // the querier's forward (or with the direction to the target) - 1 straight ahead, 0.5 sideways, 0
    // behind. A candidate at the querier, or a target at the querier, measures 0.5.
    class DotProductTest : public IEQSTest
    {
    public:
        enum class Direction : uint8_t
        {
            Forward,
            ToTarget
        };

        struct Settings
        {
            Direction direction = Direction::Forward;
        };

        explicit DotProductTest(const Settings& settings);

        bool evaluate(std::span<const EQSCandidate> candidates, const EQSContext& context,
                      const EQSProviderRefs& providers, std::span<float> metrics,
                      std::string& error) const override;

        std::string getName() const override { return "Dot"; }

    private:
        Settings settings;
    };
}
