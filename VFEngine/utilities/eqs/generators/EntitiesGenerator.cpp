#include "EntitiesGenerator.hpp"
#include <utility>

namespace eqs
{
    EntitiesGenerator::EntitiesGenerator(Settings settings)
        : settings(std::move(settings))
    {
    }

    bool EntitiesGenerator::generate(const EQSContext& context, const EQSProviderRefs& providers,
                                     std::vector<EQSCandidate>& out, std::string& error) const
    {
        if (!providers.findEntities)
        {
            error = "the Entities generator needs the scene (findEntities is not available)";
            return false;
        }
        glm::vec3 center{0.0f};
        if (!resolveContextPoint(context, settings.center, center, error, "the Entities generator"))
            return false;
        if (settings.maxCount <= 0)
            return true;

        EQSEntityFilter filter;
        filter.center = center;
        filter.radius = settings.radius;
        filter.name = settings.name;
        filter.prefix = settings.prefix;
        filter.includeInactive = settings.includeInactive;
        filter.exclude = context.querierEntityId;
        filter.maxCount = static_cast<uint32_t>(settings.maxCount);

        const std::vector<EQSEntityCandidate> found = providers.findEntities(filter);
        size_t added = 0;
        for (const EQSEntityCandidate& entity : found)
        {
            if (added >= filter.maxCount)
                break;
            // The provider already excludes the querier; skip it again so a lax provider cannot return it.
            if (entity.entityId != kNoEntity && entity.entityId == context.querierEntityId)
                continue;

            EQSCandidate candidate;
            candidate.position = entity.position;
            candidate.entityId = entity.entityId;
            out.push_back(candidate);
            ++added;
        }
        return true;
    }
}
