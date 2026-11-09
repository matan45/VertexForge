#include "DotProductTest.hpp"

namespace eqs
{
    namespace
    {
        constexpr float kMinLength = 1.0e-4f;

        glm::vec3 flatten(glm::vec3 v)
        {
            v.y = 0.0f;
            return v;
        }
    }

    DotProductTest::DotProductTest(const Settings& settings)
        : settings(settings)
    {
    }

    bool DotProductTest::evaluate(std::span<const EQSCandidate> candidates, const EQSContext& context,
                                  const EQSProviderRefs& /*providers*/, std::span<float> metrics,
                                  std::string& error) const
    {
        glm::vec3 reference{0.0f};
        if (settings.direction == Direction::ToTarget)
        {
            if (!context.hasTarget)
            {
                error = "the Dot test (direction toTarget) needs a target (the query was run without one)";
                return false;
            }
            reference = flatten(context.targetPosition - context.querierPosition);
        }
        else
        {
            reference = flatten(context.querierForward);
        }

        const float referenceLength = glm::length(reference);
        const bool hasDirection = referenceLength >= kMinLength;
        if (hasDirection)
            reference /= referenceLength;

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            const glm::vec3 toCandidate = flatten(candidates[i].position - context.querierPosition);
            const float length = glm::length(toCandidate);
            if (!hasDirection || length < kMinLength)
            {
                metrics[i] = 0.5f;
                continue;
            }
            metrics[i] = (glm::dot(reference, toCandidate / length) + 1.0f) * 0.5f;
        }
        return true;
    }
}
