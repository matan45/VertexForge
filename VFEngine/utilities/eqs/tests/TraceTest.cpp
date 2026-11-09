#include "TraceTest.hpp"

namespace eqs
{
    TraceTest::TraceTest(const Settings& settings)
        : settings(settings)
    {
    }

    bool TraceTest::evaluate(std::span<const EQSCandidate> candidates, const EQSContext& context,
                             const EQSProviderRefs& providers, std::span<float> metrics,
                             std::string& error) const
    {
        if (!providers.lineOfSight)
        {
            error = "the Trace test needs physics (lineOfSight is not available)";
            return false;
        }
        glm::vec3 from{0.0f};
        if (!resolveContextPoint(context, settings.from, from, error, "the Trace test"))
            return false;

        const glm::vec3 eye = from + glm::vec3(0.0f, settings.eyeHeight, 0.0f);
        const uint64_t lookingEntity = contextPointEntity(context, settings.from);
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            // A segment between two points (the old test passed a position where a direction was expected).
            const glm::vec3 target = candidates[i].position + glm::vec3(0.0f, settings.candidateHeight, 0.0f);
            const bool visible = providers.lineOfSight(eye, target, lookingEntity, candidates[i].entityId);
            metrics[i] = visible ? 1.0f : 0.0f;
        }
        return true;
    }
}
