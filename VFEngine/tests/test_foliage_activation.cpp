#include <doctest.h>
#include <foliage/FoliageActivation.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <cstdint>

// ============================================================
// VK-1584: proximity foliage collider activation (pure set-difference).
// Verifies the enter/leave/create/destroy logic the FoliagePhysicsActivator
// drives — no physics device, no Jolt, no scene.
// ============================================================

using foliage::FoliageColliderCandidate;
using foliage::FoliageColliderKey;
using foliage::FoliageColliderKeyHash;

namespace
{
    using ActiveMap = std::unordered_map<FoliageColliderKey, uint32_t, FoliageColliderKeyHash>;

    FoliageColliderCandidate cand(int32_t tx, int32_t tz, uint32_t seed, glm::vec3 pos, uint16_t type = 0)
    {
        FoliageColliderCandidate c;
        c.key = {tx, tz, seed};
        c.position = pos;
        c.typeIndex = type;
        return c;
    }
}

TEST_SUITE("FoliagePhysicsActivator")
{

TEST_CASE("FoliageColliderKey equality + hash key on (tile, seed)")
{
    FoliageColliderKey a{1, 2, 42};
    FoliageColliderKey b{1, 2, 42};
    FoliageColliderKey c{1, 2, 43};
    FoliageColliderKey d{9, 2, 42};
    CHECK(a == b);
    CHECK_FALSE(a == c);
    CHECK_FALSE(a == d);
    FoliageColliderKeyHash h;
    CHECK(h(a) == h(b));
}

TEST_CASE("instance entering the radius is scheduled for creation")
{
    std::vector<glm::vec3> watchers{glm::vec3(0.0f)};
    std::vector<FoliageColliderCandidate> candidates{cand(0, 0, 1, glm::vec3(5, 0, 0))};
    ActiveMap active;
    std::vector<FoliageColliderCandidate> toCreate;
    std::vector<FoliageColliderKey> toDestroy;

    foliage::computeColliderActivationDelta(watchers, 10.0f * 10.0f, candidates, active, toCreate, toDestroy);

    REQUIRE(toCreate.size() == 1);
    CHECK(toCreate[0].key == candidates[0].key);
    CHECK(toDestroy.empty());
}

TEST_CASE("instance outside the radius is neither created nor destroyed")
{
    std::vector<glm::vec3> watchers{glm::vec3(0.0f)};
    std::vector<FoliageColliderCandidate> candidates{cand(0, 0, 1, glm::vec3(100, 0, 0))};
    ActiveMap active;
    std::vector<FoliageColliderCandidate> toCreate;
    std::vector<FoliageColliderKey> toDestroy;

    foliage::computeColliderActivationDelta(watchers, 10.0f * 10.0f, candidates, active, toCreate, toDestroy);

    CHECK(toCreate.empty());
    CHECK(toDestroy.empty());
}

TEST_CASE("active instance leaving the radius is scheduled for destruction")
{
    std::vector<glm::vec3> watchers{glm::vec3(0.0f)};
    std::vector<FoliageColliderCandidate> candidates{cand(0, 0, 1, glm::vec3(100, 0, 0))}; // now far
    ActiveMap active;
    active[{0, 0, 1}] = 7u;
    std::vector<FoliageColliderCandidate> toCreate;
    std::vector<FoliageColliderKey> toDestroy;

    foliage::computeColliderActivationDelta(watchers, 10.0f * 10.0f, candidates, active, toCreate, toDestroy);

    CHECK(toCreate.empty());
    REQUIRE(toDestroy.size() == 1);
    CHECK(toDestroy[0] == FoliageColliderKey{0, 0, 1});
}

TEST_CASE("active instance whose tile streamed out (no candidate) is destroyed")
{
    std::vector<glm::vec3> watchers{glm::vec3(0.0f)};
    std::vector<FoliageColliderCandidate> candidates; // tile gone -> no candidates at all
    ActiveMap active;
    active[{2, 3, 9}] = 1u;
    std::vector<FoliageColliderCandidate> toCreate;
    std::vector<FoliageColliderKey> toDestroy;

    foliage::computeColliderActivationDelta(watchers, 10.0f * 10.0f, candidates, active, toCreate, toDestroy);

    CHECK(toCreate.empty());
    REQUIRE(toDestroy.size() == 1);
    CHECK(toDestroy[0] == FoliageColliderKey{2, 3, 9});
}

TEST_CASE("already-active in-radius instance is idempotent (no create, no destroy)")
{
    std::vector<glm::vec3> watchers{glm::vec3(0.0f)};
    std::vector<FoliageColliderCandidate> candidates{cand(0, 0, 1, glm::vec3(3, 0, 0))};
    ActiveMap active;
    active[{0, 0, 1}] = 5u;
    std::vector<FoliageColliderCandidate> toCreate;
    std::vector<FoliageColliderKey> toDestroy;

    foliage::computeColliderActivationDelta(watchers, 10.0f * 10.0f, candidates, active, toCreate, toDestroy);

    CHECK(toCreate.empty());
    CHECK(toDestroy.empty());
}

TEST_CASE("multi-watcher union: an instance near ANY watcher is created")
{
    std::vector<glm::vec3> watchers{glm::vec3(0.0f), glm::vec3(100, 0, 0)};
    std::vector<FoliageColliderCandidate> candidates{cand(0, 0, 1, glm::vec3(102, 0, 0))}; // near 2nd
    ActiveMap active;
    std::vector<FoliageColliderCandidate> toCreate;
    std::vector<FoliageColliderKey> toDestroy;

    foliage::computeColliderActivationDelta(watchers, 10.0f * 10.0f, candidates, active, toCreate, toDestroy);

    REQUIRE(toCreate.size() == 1);
    CHECK(toCreate[0].key == FoliageColliderKey{0, 0, 1});
}

TEST_CASE("radius test is XZ-only (a watcher high above still activates ground foliage)")
{
    std::vector<glm::vec3> watchers{glm::vec3(0.0f, 500.0f, 0.0f)};
    std::vector<FoliageColliderCandidate> candidates{cand(0, 0, 1, glm::vec3(3, 0, 0))};
    ActiveMap active;
    std::vector<FoliageColliderCandidate> toCreate;
    std::vector<FoliageColliderKey> toDestroy;

    foliage::computeColliderActivationDelta(watchers, 10.0f * 10.0f, candidates, active, toCreate, toDestroy);

    CHECK(toCreate.size() == 1); // in XZ range despite the 500-unit Y separation
}

TEST_CASE("determinism: repeated calls yield identical create/destroy sets")
{
    std::vector<glm::vec3> watchers{glm::vec3(0.0f)};
    std::vector<FoliageColliderCandidate> candidates{
        cand(0, 0, 1, glm::vec3(3, 0, 0)),   // in range
        cand(0, 0, 2, glm::vec3(50, 0, 0)),  // out of range
        cand(1, 0, 3, glm::vec3(-4, 0, 1))}; // in range
    ActiveMap active;
    active[{9, 9, 9}] = 1u; // stale active with no candidate -> must be destroyed

    std::vector<FoliageColliderCandidate> c1, c2;
    std::vector<FoliageColliderKey> d1, d2;
    foliage::computeColliderActivationDelta(watchers, 10.0f * 10.0f, candidates, active, c1, d1);
    foliage::computeColliderActivationDelta(watchers, 10.0f * 10.0f, candidates, active, c2, d2);

    CHECK(c1.size() == 2);           // seeds 1 and 3
    CHECK(c1.size() == c2.size());
    REQUIRE(d1.size() == 1);
    CHECK(d1.size() == d2.size());
    CHECK(d1[0] == FoliageColliderKey{9, 9, 9});
}

} // TEST_SUITE
