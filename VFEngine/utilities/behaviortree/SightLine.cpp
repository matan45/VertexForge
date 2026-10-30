#include "SightLine.hpp"

#include <algorithm>

namespace behaviortree
{
    bool isSightLineClear(std::vector<SightHit> hits, float targetDistance, float tolerance)
    {
        std::sort(hits.begin(), hits.end(), [](const SightHit& a, const SightHit& b)
        {
            return a.distance < b.distance;
        });

        for (const SightHit& hit : hits)
        {
            if (hit.kind == SightHitKind::Ignored)
            {
                continue;
            }
            return hit.kind == SightHitKind::Target || hit.distance >= targetDistance - tolerance;
        }
        return true;
    }
}
