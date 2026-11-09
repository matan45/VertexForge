#include "RingGenerator.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace eqs
{
    RingGenerator::RingGenerator(const Settings& settings)
        : settings(settings)
    {
    }

    bool RingGenerator::generate(const EQSContext& context, const EQSProviderRefs& /*providers*/,
                                 std::vector<EQSCandidate>& out, std::string& error) const
    {
        glm::vec3 center{0.0f};
        if (!resolveContextPoint(context, settings.center, center, error, "the Ring generator"))
            return false;
        if (settings.pointCount <= 0 || settings.ringCount <= 0)
            return true;

        out.reserve(out.size() + static_cast<size_t>(settings.pointCount) * static_cast<size_t>(settings.ringCount));
        const float angleStep = glm::two_pi<float>() / static_cast<float>(settings.pointCount);
        for (int ring = 0; ring < settings.ringCount; ++ring)
        {
            const float radius = settings.radius + static_cast<float>(ring) * settings.ringSpacing;
            for (int i = 0; i < settings.pointCount; ++i)
            {
                const float angle = static_cast<float>(i) * angleStep;
                EQSCandidate candidate;
                candidate.position = center + glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
                out.push_back(candidate);
            }
        }
        return true;
    }
}
