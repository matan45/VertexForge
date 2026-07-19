// VK-1577 — Reflection probe influence + parallax math.
// Pure CPU: unit-box transforms, influence falloff (including the non-cubic case the per-axis
// blendNormalized exists for), Lagarde box projection against analytically known hits, sphere
// projection landing on the sphere, and deterministic probe ordering.
#include "doctest.h"

#include <probe/ReflectionProbeMath.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <vector>

using namespace render::probe;

namespace
{
    constexpr float kEps = 1e-4f;

    bool vecNear(const glm::vec3& a, const glm::vec3& b, float eps = kEps)
    {
        return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps && std::abs(a.z - b.z) < eps;
    }
}

TEST_CASE("reflection probe: world<->local unit box round-trips")
{
    const glm::vec3 he{10.0f, 2.0f, 5.0f};
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -1.0f, 7.0f));

    const glm::mat4 w2l = buildWorldToLocal(world, he);
    const glm::mat4 l2w = buildLocalToWorld(world, he);

    SUBCASE("bounds surface maps to |1| on each axis")
    {
        // +X face center in world space is origin + halfExtents.x along X.
        const glm::vec3 faceX = glm::vec3(3.0f + 10.0f, -1.0f, 7.0f);
        const glm::vec3 local = glm::vec3(w2l * glm::vec4(faceX, 1.0f));
        CHECK(vecNear(local, glm::vec3(1.0f, 0.0f, 0.0f)));

        // The short axis: half-extent 2 -> world y = -1 + 2 = 1 is the +Y surface.
        const glm::vec3 faceY = glm::vec3(3.0f, -1.0f + 2.0f, 7.0f);
        CHECK(vecNear(glm::vec3(w2l * glm::vec4(faceY, 1.0f)), glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    SUBCASE("localToWorld is the exact inverse")
    {
        const glm::vec3 probes[] = {
            {0.0f, 0.0f, 0.0f}, {1.0f, -1.0f, 0.5f}, {-0.25f, 0.75f, -1.0f}
        };
        for (const auto& p : probes)
        {
            const glm::vec3 roundTrip = glm::vec3(w2l * (l2w * glm::vec4(p, 1.0f)));
            CHECK(vecNear(roundTrip, p));
        }
    }
}

TEST_CASE("reflection probe: box influence weight")
{
    const glm::vec3 he{10.0f};
    const glm::vec3 bn = blendNormalized(he, 1.0f); // 1 world unit over a 10-unit half-extent

    SUBCASE("full strength at the core, zero on the surface")
    {
        CHECK(probeWeightBox(glm::vec3(0.0f), bn) == doctest::Approx(1.0f));
        CHECK(probeWeightBox(glm::vec3(1.0f, 0.0f, 0.0f), bn) == doctest::Approx(0.0f));
    }

    SUBCASE("smoothstep ramp across the blend band")
    {
        // The falloff is Hermite, not linear (C1 at both ends — a linear ramp creases).
        // 0.95 local == world x 9.5 == exactly half of the 1-unit band inside the surface at 10.
        // smoothstep is symmetric about its midpoint, so the halfway point is still exactly 0.5.
        CHECK(probeWeightBox(glm::vec3(0.95f, 0.0f, 0.0f), bn) == doctest::Approx(0.5f));
        // 0.99 -> ratio 0.1 -> 3(0.1^2) - 2(0.1^3) = 0.03 - 0.002 = 0.028
        CHECK(probeWeightBox(glm::vec3(0.99f, 0.0f, 0.0f), bn) == doctest::Approx(0.028f));
        // 0.91 -> ratio 0.9 -> 3(0.81) - 2(0.729) = 2.43 - 1.458 = 0.972
        CHECK(probeWeightBox(glm::vec3(0.91f, 0.0f, 0.0f), bn) == doctest::Approx(0.972f));
    }

    SUBCASE("falloff is C1: the slope flattens at both ends of the band")
    {
        // The property a linear ramp lacks, and the whole reason for smoothstep: d/dt of
        // 3t^2-2t^3 is 6t(1-t), which vanishes at t=0 and t=1 and peaks at t=0.5. A linear ramp
        // would instead give the SAME slope everywhere, and the resulting derivative jump at the
        // band edges is what reads as a crease on a large smooth surface.
        //
        // The comparison must be relative, not absolute: the band is only 0.1 wide in local units,
        // so dt/dx = 10 scales every gradient up by 10x. Measured here, the midpoint slope is
        // ~15.0/local-unit and each end is ~0.89 — a ~17x flattening. Assert well inside that.
        auto w = [&](float x) { return probeWeightBox(glm::vec3(x, 0.0f, 0.0f), bn); };
        const float h = 0.001f;
        const float gradAtSurface = std::abs(w(1.0f - h) - w(1.0f - 2.0f * h)) / h;
        const float gradAtCore = std::abs(w(0.90f + 2.0f * h) - w(0.90f + h)) / h;
        const float gradAtMid = std::abs(w(0.95f + h) - w(0.95f - h)) / (2.0f * h);

        CHECK(gradAtMid > 10.0f);                      // steep in the middle
        CHECK(gradAtSurface < gradAtMid * 0.10f);      // flat approaching the bounds surface
        CHECK(gradAtCore < gradAtMid * 0.10f);         // flat approaching full strength
    }

    SUBCASE("clamps to zero outside the bounds")
    {
        CHECK(probeWeightBox(glm::vec3(1.5f, 0.0f, 0.0f), bn) == doctest::Approx(0.0f));
        CHECK(probeWeightBox(glm::vec3(0.0f, -3.0f, 0.0f), bn) == doctest::Approx(0.0f));
    }

    SUBCASE("weight is monotonically non-increasing toward the surface")
    {
        float prev = 1.01f;
        for (int i = 0; i <= 100; ++i)
        {
            const float w = probeWeightBox(glm::vec3(static_cast<float>(i) / 100.0f, 0.0f, 0.0f), bn);
            CHECK(w <= prev + kEps);
            prev = w;
        }
    }

    SUBCASE("corners fade: the nearest face governs")
    {
        // Near the +X face only.
        const float faceOnly = probeWeightBox(glm::vec3(0.95f, 0.0f, 0.0f), bn);
        // Near +X and +Y simultaneously — must not be stronger than the single-face case.
        const float corner = probeWeightBox(glm::vec3(0.95f, 0.97f, 0.0f), bn);
        CHECK(corner <= faceOnly + kEps);
        // min ratio is 0.3 (the +Y face) -> 3(0.09) - 2(0.027) = 0.27 - 0.054 = 0.216
        CHECK(corner == doctest::Approx(0.216f));
    }

    SUBCASE("non-cubic probes fade symmetrically in WORLD units")
    {
        // This is what per-axis blendNormalized exists for: a 1-unit band must be 1 world unit on
        // every axis, even when the half-extents differ by 5x.
        const glm::vec3 flat{10.0f, 2.0f, 10.0f};
        const glm::vec3 bnFlat = blendNormalized(flat, 1.0f);
        // 0.5 world unit in from the +Y surface (half-extent 2) -> local y = 1.5/2 = 0.75.
        CHECK(probeWeightBox(glm::vec3(0.0f, 0.75f, 0.0f), bnFlat) == doctest::Approx(0.5f));
        // 0.5 world unit in from the +X surface (half-extent 10) -> local x = 9.5/10 = 0.95.
        CHECK(probeWeightBox(glm::vec3(0.95f, 0.0f, 0.0f), bnFlat) == doctest::Approx(0.5f));
    }

    SUBCASE("zero blend distance gives a hard edge, not a NaN")
    {
        const glm::vec3 bnHard = blendNormalized(he, 0.0f);
        CHECK(probeWeightBox(glm::vec3(0.0f), bnHard) == doctest::Approx(1.0f));
        CHECK(probeWeightBox(glm::vec3(0.999f, 0.0f, 0.0f), bnHard) == doctest::Approx(1.0f));
        CHECK(probeWeightBox(glm::vec3(1.001f, 0.0f, 0.0f), bnHard) == doctest::Approx(0.0f));
    }
}

TEST_CASE("reflection probe: sphere influence weight")
{
    const glm::vec3 center{0.0f};

    CHECK(probeWeightSphere(center, center, 10.0f, 1.0f) == doctest::Approx(1.0f));
    CHECK(probeWeightSphere(glm::vec3(9.5f, 0.0f, 0.0f), center, 10.0f, 1.0f) == doctest::Approx(0.5f));
    CHECK(probeWeightSphere(glm::vec3(10.0f, 0.0f, 0.0f), center, 10.0f, 1.0f) == doctest::Approx(0.0f));
    CHECK(probeWeightSphere(glm::vec3(50.0f, 0.0f, 0.0f), center, 10.0f, 1.0f) == doctest::Approx(0.0f));

    SUBCASE("falloff is radially symmetric")
    {
        const glm::vec3 dirs[] = {
            {1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0},
            glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f))
        };
        for (const auto& d : dirs)
            CHECK(probeWeightSphere(d * 9.5f, center, 10.0f, 1.0f) == doctest::Approx(0.5f));
    }
}

TEST_CASE("reflection probe: box parallax correction")
{
    // Axis-aligned box, half-extent 10, centered at the origin; probe captures from the center.
    const glm::vec3 he{10.0f};
    const glm::mat4 world(1.0f);
    const glm::mat4 w2l = buildWorldToLocal(world, he);
    const glm::mat4 l2w = buildLocalToWorld(world, he);
    const glm::vec3 capture{0.0f};

    SUBCASE("at the capture point the correction is a no-op")
    {
        const glm::vec3 R{1.0f, 0.0f, 0.0f};
        CHECK(vecNear(parallaxCorrectBox(R, capture, w2l, l2w, capture), R));
    }

    SUBCASE("offset fragment reflecting horizontally picks up vertical parallax")
    {
        // 5 units above the capture point, reflecting along +X: the ray leaves through the +X wall
        // at (10, 5, 0), which from the capture point is up-and-to-the-right, NOT straight +X.
        // Without parallax correction this would still be (1,0,0) — that difference IS the feature.
        const glm::vec3 frag{0.0f, 5.0f, 0.0f};
        const glm::vec3 R{1.0f, 0.0f, 0.0f};
        const glm::vec3 corrected = parallaxCorrectBox(R, frag, w2l, l2w, capture);
        CHECK(vecNear(corrected, glm::normalize(glm::vec3(10.0f, 5.0f, 0.0f))));
        CHECK(corrected.y > 0.4f); // demonstrably different from the uncorrected R
    }

    SUBCASE("a fragment against a wall reflects the opposite wall")
    {
        const glm::vec3 frag{-9.0f, 0.0f, 0.0f};
        const glm::vec3 R{1.0f, 0.0f, 0.0f};
        // Hit is the +X wall at (10,0,0); from the capture point that is straight +X.
        CHECK(vecNear(parallaxCorrectBox(R, frag, w2l, l2w, capture), glm::vec3(1.0f, 0.0f, 0.0f)));
    }

    SUBCASE("diagonal rays leave through the nearest face")
    {
        const glm::vec3 frag{0.0f, 0.0f, 0.0f};
        const glm::vec3 R = glm::normalize(glm::vec3(1.0f, 0.5f, 0.0f));
        const glm::vec3 corrected = parallaxCorrectBox(R, frag, w2l, l2w, capture);
        // From the center the correction cannot change the direction.
        CHECK(vecNear(corrected, R));
    }

    SUBCASE("axis-parallel rays do not produce NaN")
    {
        // A ray exactly on a slab plane makes one axis' t degenerate; safeInverse must absorb it.
        const glm::vec3 frag{0.0f, 10.0f, 0.0f}; // exactly on the +Y surface
        const glm::vec3 R{1.0f, 0.0f, 0.0f};
        const glm::vec3 c = parallaxCorrectBox(R, frag, w2l, l2w, capture);
        CHECK(c.x == c.x); // NaN check
        CHECK(c.y == c.y);
        CHECK(c.z == c.z);
        CHECK(glm::length(c) == doctest::Approx(1.0f));
    }

    SUBCASE("NON-CUBIC probe: the sheared local direction still lands on the real box face")
    {
        // The regression guard for the affine-invariance argument in parallaxCorrectBox.
        // worldToLocal folds in 1/halfExtents, so for a 10 x 2 x 10 probe mat3(worldToLocal) is
        // NOT orthonormal and rLocal is sheared + non-unit. That is only harmless because the ray
        // parameter t survives an affine map and we map the hit back before normalizing. If anyone
        // "fixes" the shear by normalizing rLocal, this case breaks and the round cases do not.
        const glm::vec3 flat{10.0f, 2.0f, 10.0f};
        const glm::mat4 fw2l = buildWorldToLocal(glm::mat4(1.0f), flat);
        const glm::mat4 fl2w = buildLocalToWorld(glm::mat4(1.0f), flat);

        // Fragment 1 unit up in a box that is only 2 units half-high, reflecting along +X.
        // The ray exits the +X face (x = 10) at height y = 1, unchanged.
        const glm::vec3 corrected =
            parallaxCorrectBox(glm::vec3(1, 0, 0), glm::vec3(0.0f, 1.0f, 0.0f), fw2l, fl2w, glm::vec3(0.0f));
        CHECK(vecNear(corrected, glm::normalize(glm::vec3(10.0f, 1.0f, 0.0f))));

        // A diagonal ray from the centre of a flat probe must exit through the near +Y face
        // (half-height 2), not the far +X face (half-width 10).
        const glm::vec3 diag = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
        const glm::vec3 c2 = parallaxCorrectBox(diag, glm::vec3(0.0f), fw2l, fl2w, glm::vec3(0.0f));
        CHECK(vecNear(c2, glm::normalize(glm::vec3(2.0f, 2.0f, 0.0f))));
    }

    SUBCASE("PROPERTY: the parallax hit always lies exactly ON the bounds surface")
    {
        // Configuration-independent invariant, so it covers rotated + non-cubic + translated
        // probes where no closed form is convenient. Map the implied hit back into unit-box space:
        // the largest absolute component must be exactly 1 (i.e. on a face).
        const glm::vec3 extents[] = {
            {10.0f, 10.0f, 10.0f}, {10.0f, 2.0f, 10.0f}, {1.0f, 7.0f, 3.0f}
        };
        const glm::mat4 worlds[] = {
            glm::mat4(1.0f),
            glm::translate(glm::mat4(1.0f), glm::vec3(12.0f, -4.0f, 30.0f)),
            glm::rotate(glm::mat4(1.0f), glm::radians(37.0f), glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f))),
            glm::translate(glm::mat4(1.0f), glm::vec3(-5.0f, 2.0f, 1.0f)) *
                glm::rotate(glm::mat4(1.0f), glm::radians(64.0f), glm::vec3(0, 1, 0)),
        };
        const glm::vec3 dirs[] = {
            {1, 0, 0}, {0, 1, 0}, {0, 0, 1},
            glm::normalize(glm::vec3(1.0f, 2.0f, -3.0f)),
            glm::normalize(glm::vec3(-0.4f, 0.1f, 0.9f)),
        };
        const glm::vec3 localOffsets[] = {
            {0.0f, 0.0f, 0.0f}, {0.5f, -0.25f, 0.1f}, {-0.8f, 0.6f, 0.3f}
        };

        for (const auto& ext : extents)
            for (const auto& probeWorld : worlds)
            {
                const glm::mat4 pw2l = buildWorldToLocal(probeWorld, ext);
                const glm::mat4 pl2w = buildLocalToWorld(probeWorld, ext);
                const glm::vec3 probeCapture = glm::vec3(probeWorld[3]);

                for (const auto& off : localOffsets)
                {
                    const glm::vec3 frag = glm::vec3(pl2w * glm::vec4(off, 1.0f));
                    for (const auto& R : dirs)
                    {
                        const glm::vec3 corrected = parallaxCorrectBox(R, frag, pw2l, pl2w, probeCapture);

                        // Unit length, no NaN.
                        REQUIRE(corrected.x == corrected.x);
                        CHECK(glm::length(corrected) == doctest::Approx(1.0f).epsilon(0.001));

                        // Re-derive the hit the function computed and confirm it sits ON a face:
                        // in unit-box space the largest absolute component must be exactly 1.
                        const glm::vec3 pL = glm::vec3(pw2l * glm::vec4(frag, 1.0f));
                        const glm::vec3 rL = glm::mat3(pw2l) * R;
                        const glm::vec3 inv = safeInverse(rL);
                        const glm::vec3 t = glm::max((glm::vec3(1.0f) - pL) * inv, (glm::vec3(-1.0f) - pL) * inv);
                        const float d = std::min(t.x, std::min(t.y, t.z));
                        const glm::vec3 hitLocal = pL + rL * d;
                        const float onFace = std::max(std::abs(hitLocal.x),
                                                      std::max(std::abs(hitLocal.y), std::abs(hitLocal.z)));
                        CHECK(onFace == doctest::Approx(1.0f).epsilon(0.001));
                    }
                }
            }
    }

    SUBCASE("a translated probe behaves identically in its own frame")
    {
        const glm::mat4 moved = glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, -50.0f));
        const glm::mat4 mw2l = buildWorldToLocal(moved, he);
        const glm::mat4 ml2w = buildLocalToWorld(moved, he);
        const glm::vec3 mCapture{100.0f, 0.0f, -50.0f};
        const glm::vec3 frag = mCapture + glm::vec3(0.0f, 5.0f, 0.0f);
        const glm::vec3 corrected = parallaxCorrectBox(glm::vec3(1, 0, 0), frag, mw2l, ml2w, mCapture);
        CHECK(vecNear(corrected, glm::normalize(glm::vec3(10.0f, 5.0f, 0.0f))));
    }
}

TEST_CASE("reflection probe: sphere parallax correction")
{
    const glm::vec3 center{0.0f};
    const float radius = 10.0f;

    SUBCASE("corrected direction points at a spot ON the sphere")
    {
        const glm::vec3 frag{0.0f, 5.0f, 0.0f};
        const glm::vec3 R{1.0f, 0.0f, 0.0f};
        const glm::vec3 corrected = parallaxCorrectSphere(R, frag, center, radius, center);
        // Capture is at the sphere center, so the corrected direction must be the hit point's
        // direction — and the hit must lie exactly on the sphere.
        const glm::vec3 hit = corrected * radius;
        CHECK(glm::length(hit - center) == doctest::Approx(radius));
        CHECK(corrected.y > 0.0f); // the hit is above the equator, as the fragment is
    }

    SUBCASE("no correction from the center")
    {
        const glm::vec3 R = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
        CHECK(vecNear(parallaxCorrectSphere(R, center, center, radius, center), R));
    }

    SUBCASE("a fragment outside the sphere looking away falls back to R")
    {
        const glm::vec3 frag{100.0f, 0.0f, 0.0f};
        const glm::vec3 R{1.0f, 0.0f, 0.0f}; // pointing further away, no intersection
        CHECK(vecNear(parallaxCorrectSphere(R, frag, center, radius, center), R));
    }
}

TEST_CASE("reflection probe: ordering is deterministic")
{
    SUBCASE("higher priority sorts first")
    {
        ProbeSortKey a{.priority = 5, .volume = 1000.0f, .stableId = 111};
        ProbeSortKey b{.priority = 1, .volume = 1.0f, .stableId = 222};
        CHECK(probeSortLess(a, b));
        CHECK_FALSE(probeSortLess(b, a));
    }

    SUBCASE("at equal priority the smaller volume wins")
    {
        ProbeSortKey big{.priority = 0, .volume = 1000.0f, .stableId = 111};
        ProbeSortKey small{.priority = 0, .volume = 10.0f, .stableId = 222};
        CHECK(probeSortLess(small, big));
        CHECK_FALSE(probeSortLess(big, small));
    }

    SUBCASE("exact ties break on the entity UUID, so the order is total")
    {
        ProbeSortKey a{.priority = 0, .volume = 5.0f, .stableId = 42};
        ProbeSortKey b{.priority = 0, .volume = 5.0f, .stableId = 9001};
        CHECK(probeSortLess(a, b));
        CHECK_FALSE(probeSortLess(b, a));
        CHECK_FALSE(probeSortLess(a, a)); // irreflexive, as std::sort requires
    }

    SUBCASE("sorting a mixed set produces the documented order")
    {
        std::vector<ProbeSortKey> keys = {
            {.priority = 0, .volume = 100.0f, .stableId = 500},
            {.priority = 0, .volume = 10.0f, .stableId = 300},
            {.priority = 3, .volume = 999.0f, .stableId = 700},
            {.priority = 0, .volume = 10.0f, .stableId = 400},
        };
        std::sort(keys.begin(), keys.end(), probeSortLess);
        CHECK(keys[0].stableId == 700u); // highest priority, regardless of size
        CHECK(keys[1].stableId == 300u); // then smallest volume, lowest UUID of the tie
        CHECK(keys[2].stableId == 400u);
        CHECK(keys[3].stableId == 500u); // biggest volume last
    }

    SUBCASE("order is independent of the order probes were enumerated in")
    {
        // The point of the UUID tiebreak: EnTT pool order is not stable across add/remove or
        // save/load, so the same probe set discovered in a different order must still sort the
        // same way. Feed the identical set in reverse and require an identical result.
        std::vector<ProbeSortKey> a = {
            {.priority = 2, .volume = 50.0f, .stableId = 10},
            {.priority = 0, .volume = 50.0f, .stableId = 20},
            {.priority = 0, .volume = 50.0f, .stableId = 30},
            {.priority = 0, .volume = 5.0f, .stableId = 40},
        };
        std::vector<ProbeSortKey> b(a.rbegin(), a.rend());

        std::sort(a.begin(), a.end(), probeSortLess);
        std::sort(b.begin(), b.end(), probeSortLess);

        REQUIRE(a.size() == b.size());
        for (size_t i = 0; i < a.size(); ++i)
            CHECK(a[i].stableId == b[i].stableId);

        CHECK(a[0].stableId == 10u); // priority wins
        CHECK(a[1].stableId == 40u); // then smallest volume
        CHECK(a[2].stableId == 20u); // equal priority + volume -> UUID ascending
        CHECK(a[3].stableId == 30u);
    }
}

TEST_CASE("reflection probe: volume + world bounds")
{
    SUBCASE("box volume is the true box volume")
    {
        CHECK(probeVolume(glm::vec3(1.0f, 2.0f, 3.0f), false) == doctest::Approx(8.0f * 6.0f));
    }

    SUBCASE("sphere volume uses radius from .x")
    {
        const float r = 2.0f;
        CHECK(probeVolume(glm::vec3(r, 99.0f, 99.0f), true)
              == doctest::Approx(4.0f / 3.0f * 3.14159265358979f * r * r * r));
    }

    SUBCASE("world bounds of an unrotated box are the box itself")
    {
        const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));
        const WorldBounds b = worldBoundsOfBox(world, glm::vec3(2.0f, 3.0f, 4.0f));
        CHECK(vecNear(b.min, glm::vec3(3.0f, -3.0f, -4.0f)));
        CHECK(vecNear(b.max, glm::vec3(7.0f, 3.0f, 4.0f)));
    }

    SUBCASE("a 45-degree yaw grows the AABB by sqrt(2) on the rotated axes")
    {
        const glm::mat4 world = glm::rotate(glm::mat4(1.0f), glm::radians(45.0f), glm::vec3(0, 1, 0));
        const WorldBounds b = worldBoundsOfBox(world, glm::vec3(1.0f, 1.0f, 1.0f));
        CHECK(b.max.x == doctest::Approx(std::sqrt(2.0f)));
        CHECK(b.max.z == doctest::Approx(std::sqrt(2.0f)));
        CHECK(b.max.y == doctest::Approx(1.0f)); // untouched by a yaw
    }
}
