#include "DistanceTest.hpp"

namespace eqs
{
    DistanceTest::DistanceTest(const Settings& settings)
        : settings(settings)
    {
    }

    bool DistanceTest::evaluate(std::span<const EQSCandidate> candidates, const EQSContext& context,
                                const EQSProviderRefs& /*providers*/, std::span<float> metrics,
                                std::string& error) const
    {
        glm::vec3 reference{0.0f};
        if (!resolveContextPoint(context, settings.to, reference, error, "the Distance test"))
            return false;

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            glm::vec3 delta = candidates[i].position - reference;
            if (settings.horizontal)
                delta.y = 0.0f;
            metrics[i] = glm::length(delta);
        }
        return true;
    }
}
