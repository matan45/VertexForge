#include <doctest.h>
#include <impl/physics/CharacterInterpMath.hpp>
#include <glm/glm.hpp>

// ============================================================
// VK-1530 fix — character-controller render interpolation.
//
// The Tests project is CPU-only and links Services (not Core/Jolt), so the two-snapshot
// bracket + blend math was factored into the glm-only header
// services/impl/physics/CharacterInterpMath.hpp. These tests pin the behavior the earlier
// single-snapshot code got wrong: on a zero-step frame the render must SWEEP between the two
// persistent snapshots as alpha grows, not pin to the latest sim position.
// ============================================================

using services::physics::CharacterInterp;
using services::physics::seedCharacterInterp;
using services::physics::pushSimStep;
using services::physics::snapOnTeleport;
using services::physics::interpRenderPos;

namespace
{
    constexpr float TELEPORT2 = 50.0f * 50.0f;

    void checkVec(const glm::vec3& got, const glm::vec3& want)
    {
        CHECK(got.x == doctest::Approx(want.x));
        CHECK(got.y == doctest::Approx(want.y));
        CHECK(got.z == doctest::Approx(want.z));
    }
}

TEST_CASE("character interp: zero-step frames interpolate instead of pinning")
{
    CharacterInterp s;
    seedCharacterInterp(s, glm::vec3(0.0f));
    CHECK(s.seeded);

    // One sub-step advances the bracket to [P0=(0,0,0), P1=(1,0,0)].
    pushSimStep(s, glm::vec3(1.0f, 0.0f, 0.0f));
    checkVec(s.prev, glm::vec3(0.0f));
    checkVec(s.curr, glm::vec3(1.0f, 0.0f, 0.0f));

    // Subsequent frames run ZERO sub-steps: the bracket must stay put while alpha sweeps the
    // render from prev toward curr. The old bug returned a constant here (prev == curr).
    checkVec(interpRenderPos(s, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f));
    checkVec(interpRenderPos(s, 0.25f), glm::vec3(0.25f, 0.0f, 0.0f));
    checkVec(interpRenderPos(s, 0.5f), glm::vec3(0.5f, 0.0f, 0.0f));
    checkVec(interpRenderPos(s, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f));

    // Strictly monotonic in alpha (and not pinned to curr for alpha < 1).
    CHECK(interpRenderPos(s, 0.25f).x < interpRenderPos(s, 0.75f).x);
    CHECK(interpRenderPos(s, 0.5f).x < s.curr.x);
}

TEST_CASE("character interp: a new sub-step continues seamlessly from curr")
{
    CharacterInterp s;
    seedCharacterInterp(s, glm::vec3(0.0f));
    pushSimStep(s, glm::vec3(1.0f, 0.0f, 0.0f));   // bracket [P0, P1]; we render ~P1 as alpha->1

    // Next step frame shifts the bracket to [P1, P2]; render at the fresh (small) alpha must
    // resume exactly at P1 — the position we had swept to — so there is no visible jump.
    pushSimStep(s, glm::vec3(2.0f, 0.0f, 0.0f));
    checkVec(s.prev, glm::vec3(1.0f, 0.0f, 0.0f));
    checkVec(s.curr, glm::vec3(2.0f, 0.0f, 0.0f));
    checkVec(interpRenderPos(s, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    checkVec(interpRenderPos(s, 1.0f), glm::vec3(2.0f, 0.0f, 0.0f));
}

TEST_CASE("character interp: a multi-step frame brackets the last two sub-steps")
{
    CharacterInterp s;
    seedCharacterInterp(s, glm::vec3(0.0f));
    pushSimStep(s, glm::vec3(1.0f, 0.0f, 0.0f));   // sub-step 1
    pushSimStep(s, glm::vec3(2.0f, 0.0f, 0.0f));   // sub-step 2 (same frame)
    checkVec(s.prev, glm::vec3(1.0f, 0.0f, 0.0f));
    checkVec(s.curr, glm::vec3(2.0f, 0.0f, 0.0f));
}

TEST_CASE("character interp: teleport collapses the bracket, no streak")
{
    CharacterInterp s;
    seedCharacterInterp(s, glm::vec3(0.0f));
    pushSimStep(s, glm::vec3(1.0f, 0.0f, 0.0f));   // bracket [P0, P1]

    const glm::vec3 beforeFrame = s.curr;          // = P1, as it stands before this frame
    pushSimStep(s, glm::vec3(1000.0f, 0.0f, 0.0f)); // huge jump this frame (teleport/respawn)
    snapOnTeleport(s, beforeFrame, TELEPORT2);

    // Bracket collapsed onto curr: every alpha renders the destination (no interpolation streak).
    checkVec(s.prev, glm::vec3(1000.0f, 0.0f, 0.0f));
    checkVec(interpRenderPos(s, 0.0f), glm::vec3(1000.0f, 0.0f, 0.0f));
    checkVec(interpRenderPos(s, 0.5f), glm::vec3(1000.0f, 0.0f, 0.0f));
}

TEST_CASE("character interp: a normal step is NOT treated as a teleport")
{
    CharacterInterp s;
    seedCharacterInterp(s, glm::vec3(0.0f));
    pushSimStep(s, glm::vec3(1.0f, 0.0f, 0.0f));

    const glm::vec3 beforeFrame = s.curr;
    pushSimStep(s, glm::vec3(1.5f, 0.0f, 0.0f));   // ordinary walk distance
    snapOnTeleport(s, beforeFrame, TELEPORT2);

    checkVec(s.prev, glm::vec3(1.0f, 0.0f, 0.0f));  // bracket preserved
    checkVec(interpRenderPos(s, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    checkVec(interpRenderPos(s, 1.0f), glm::vec3(1.5f, 0.0f, 0.0f));
}

TEST_CASE("character interp: alpha is clamped so a hitch never extrapolates")
{
    CharacterInterp s;
    seedCharacterInterp(s, glm::vec3(0.0f));
    pushSimStep(s, glm::vec3(1.0f, 0.0f, 0.0f));

    checkVec(interpRenderPos(s, 1.5f), glm::vec3(1.0f, 0.0f, 0.0f));   // clamped to curr
    checkVec(interpRenderPos(s, -0.5f), glm::vec3(0.0f, 0.0f, 0.0f));  // clamped to prev
}
