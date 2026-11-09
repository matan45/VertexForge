#include "NavMeshPointsGenerator.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>
#include <random>

namespace eqs
{
    NavMeshPointsGenerator::NavMeshPointsGenerator(const Settings& settings)
        : settings(settings)
    {
    }

    bool NavMeshPointsGenerator::generate(const EQSContext& context, const EQSProviderRefs& providers,
                                          std::vector<EQSCandidate>& out, std::string& error) const
    {
        if (!providers.projectToNavmesh)
        {
            error = "the NavMeshPoints generator needs the navmesh (projectToNavmesh is not available)";
            return false;
        }
        glm::vec3 center{0.0f};
        if (!resolveContextPoint(context, settings.center, center, error, "the NavMeshPoints generator"))
            return false;
        if (settings.pointCount <= 0)
            return true;

        // Seeded, so a query is reproducible for a given (entity, node, tick) seed.
        std::mt19937_64 rng(context.seed);
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);

        out.reserve(out.size() + static_cast<size_t>(settings.pointCount));
        for (int i = 0; i < settings.pointCount; ++i)
        {
            const float angle = unit(rng) * glm::two_pi<float>();
            const float radius = std::sqrt(unit(rng)) * settings.radius; // uniform over the disc's area
            const glm::vec3 sample = center + glm::vec3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);

            const std::optional<glm::vec3> projected = providers.projectToNavmesh(sample, settings.snapRadius);
            if (!projected)
                continue;

            EQSCandidate candidate;
            candidate.position = *projected;
            out.push_back(candidate);
        }
        return true;
    }
}
