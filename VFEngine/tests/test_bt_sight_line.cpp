#include <doctest.h>
#include <behaviortree/SightLine.hpp>
#include <vector>

// ============================================================
// VK-1660: line-of-sight hit classification (behaviortree::isSightLineClear): the looker's own
// colliders never block, a hit on the target means visible, anything nearer than the target blocks.
// ============================================================

namespace
{
    namespace bt = behaviortree;

    bt::SightHit hit(float distance, bt::SightHitKind kind)
    {
        bt::SightHit h;
        h.distance = distance;
        h.kind = kind;
        return h;
    }
}

TEST_SUITE("BTSightLine")
{
    TEST_CASE("no hits: visible")
    {
        CHECK(bt::isSightLineClear({}, 10.0f, 0.1f));
    }

    TEST_CASE("the looker's own collider (hit at the eye) does not block")
    {
        CHECK(bt::isSightLineClear({hit(0.0f, bt::SightHitKind::Ignored)}, 10.0f, 0.1f));
        CHECK(bt::isSightLineClear({hit(0.0f, bt::SightHitKind::Ignored), hit(9.6f, bt::SightHitKind::Target)},
                                   10.0f, 0.1f));
    }

    TEST_CASE("a hit on the target itself is visible, even well before the target point")
    {
        CHECK(bt::isSightLineClear({hit(9.5f, bt::SightHitKind::Target)}, 10.0f, 0.1f));
    }

    TEST_CASE("a blocker before the target hides it; a blocker past the target does not")
    {
        CHECK_FALSE(bt::isSightLineClear({hit(4.0f, bt::SightHitKind::Blocker), hit(9.5f, bt::SightHitKind::Target)},
                                         10.0f, 0.1f));
        CHECK(bt::isSightLineClear({hit(9.95f, bt::SightHitKind::Blocker)}, 10.0f, 0.1f)); // within tolerance
        CHECK_FALSE(bt::isSightLineClear({hit(9.85f, bt::SightHitKind::Blocker)}, 10.0f, 0.1f));
    }

    TEST_CASE("hits are ordered by distance before classification")
    {
        // Unsorted input: the target (9.5) is behind a wall (3.0) - hidden.
        CHECK_FALSE(bt::isSightLineClear({hit(9.5f, bt::SightHitKind::Target), hit(3.0f, bt::SightHitKind::Blocker),
                                          hit(0.0f, bt::SightHitKind::Ignored)},
                                         10.0f, 0.1f));
        // The target (2.0) stands in front of the wall (6.0) - visible.
        CHECK(bt::isSightLineClear({hit(6.0f, bt::SightHitKind::Blocker), hit(2.0f, bt::SightHitKind::Target)},
                                   10.0f, 0.1f));
    }
}
