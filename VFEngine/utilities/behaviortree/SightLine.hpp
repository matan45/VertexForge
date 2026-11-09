#pragma once

#include <cstdint>
#include <vector>

// VK-1660: which physics hits block a behavior-tree line-of-sight ray, free of the engine so it is
// CPU-testable. The executor casts one ray from the looker's eye to the target point, classifies every hit
// and asks isSightLineClear.
namespace behaviortree
{
    enum class SightHitKind : uint8_t
    {
        Ignored, // the looker's own collider, or one of its descendants'
        Target,  // the target entity's collider, or one of its descendants'
        Blocker  // anything else (including hits with no entity, e.g. terrain)
    };

    struct SightHit
    {
        float distance = 0.0f; // along the ray, from the eye
        SightHitKind kind = SightHitKind::Blocker;
    };

    // True when the target, `targetDistance` along the ray, is visible: the nearest hit that is not Ignored
    // is the Target itself or lies no nearer than `targetDistance - tolerance` (at or past the target point),
    // or there is no such hit. `hits` may be in any order.
    bool isSightLineClear(std::vector<SightHit> hits, float targetDistance, float tolerance);
}
