#include "GridGenerator.hpp"

namespace eqs
{
    GridGenerator::GridGenerator(const Settings& settings)
        : settings(settings)
    {
    }

    bool GridGenerator::generate(const EQSContext& context, const EQSProviderRefs& /*providers*/,
                                 std::vector<EQSCandidate>& out, std::string& error) const
    {
        glm::vec3 center{0.0f};
        if (!resolveContextPoint(context, settings.center, center, error, "the Grid generator"))
            return false;
        if (settings.size <= 0)
            return true;

        out.reserve(out.size() + static_cast<size_t>(settings.size) * static_cast<size_t>(settings.size));
        const float halfExtent = static_cast<float>(settings.size - 1) * settings.spacing * 0.5f;
        for (int z = 0; z < settings.size; ++z)
        {
            for (int x = 0; x < settings.size; ++x)
            {
                EQSCandidate candidate;
                candidate.position = center + glm::vec3(static_cast<float>(x) * settings.spacing - halfExtent,
                                                        0.0f,
                                                        static_cast<float>(z) * settings.spacing - halfExtent);
                out.push_back(candidate);
            }
        }
        return true;
    }
}
