#include <doctest.h>

// VK-1585: procedural MESH scatter into the packed FoliageInstance store. Reuses the deterministic
// vegetation rule taxonomy but emits FoliageInstance; these tests cover the ticket's "mesh-scatter
// determinism (same seed -> identical instance set)" plus flag tagging, budget, and gate reuse.

#include "foliage/FoliageScatterBaker.hpp"
#include "foliage/FoliageTypes.hpp"
#include "vegetation/VegetationScatterTypes.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

using namespace foliage;

namespace
{
    auto flatHeight = [](float, float) { return 0.0f; };
    auto flatNormal = [](float, float) { return glm::vec3(0.0f, 1.0f, 0.0f); };
    auto fullLayer = [](uint8_t, float, float) { return 1.0f; };
    auto zeroCurv = [](float, float) { return 0.0f; };

    vegetation::ScatterProfile oneRuleProfile(uint32_t entry, float density, float spacing, float jitter)
    {
        vegetation::ScatterProfile p;
        vegetation::ScatterRule r;
        r.paletteEntryIndex = entry;
        r.density = density;
        r.spacing = spacing;
        r.positionJitter = jitter;
        p.rules.push_back(r);
        return p;
    }

    std::vector<FoliageType> onePalette()
    {
        std::vector<FoliageType> pal(1);
        pal[0].meshPath = "tree.vfMesh";
        return pal;
    }
}

TEST_SUITE("FoliageScatter")
{
    TEST_CASE("mesh scatter is deterministic (same seed -> identical instance set)")
    {
        auto p = oneRuleProfile(0, 1.0f, 1.0f, 0.5f);
        auto pal = onePalette();

        auto a = bakeFoliageScatterForTile(p, pal, 1337u, 0, 0, 8.0f,
                                           flatHeight, flatNormal, fullLayer, zeroCurv, 1u << 20);
        auto b = bakeFoliageScatterForTile(p, pal, 1337u, 0, 0, 8.0f,
                                           flatHeight, flatNormal, fullLayer, zeroCurv, 1u << 20);

        REQUIRE(a.instances.size() == b.instances.size());
        REQUIRE(a.instances.size() > 0);
        for (size_t i = 0; i < a.instances.size(); ++i)
        {
            const auto& x = a.instances[i];
            const auto& y = b.instances[i];
            CHECK(x.position.x == doctest::Approx(y.position.x));
            CHECK(x.position.y == doctest::Approx(y.position.y));
            CHECK(x.position.z == doctest::Approx(y.position.z));
            CHECK(x.rotationY == doctest::Approx(y.rotationY));
            CHECK(x.scale.x == doctest::Approx(y.scale.x));
            CHECK(x.scale.y == doctest::Approx(y.scale.y));
            CHECK(x.typeIndex == y.typeIndex);
            CHECK(x.flags == y.flags);
            CHECK(x.windPhase == doctest::Approx(y.windPhase));
            CHECK(x.seed == y.seed);
        }
    }

    TEST_CASE("all baked instances are tagged Procedural")
    {
        auto p = oneRuleProfile(0, 1.0f, 1.0f, 0.5f);
        auto pal = onePalette();
        auto r = bakeFoliageScatterForTile(p, pal, 9u, 0, 0, 8.0f,
                                           flatHeight, flatNormal, fullLayer, zeroCurv, 1u << 20);
        REQUIRE(r.instances.size() > 0);
        for (const auto& fi : r.instances)
        {
            CHECK((fi.flags & FoliageInstanceFlags::Procedural) != 0);
            CHECK(fi.typeIndex == 0);
        }
    }

    TEST_CASE("collision / navContribute flags propagate from the FoliageType")
    {
        auto p = oneRuleProfile(0, 1.0f, 1.0f, 0.0f);
        auto pal = onePalette();
        pal[0].collision = true;
        pal[0].navContribute = true;
        auto r = bakeFoliageScatterForTile(p, pal, 4u, 0, 0, 8.0f,
                                           flatHeight, flatNormal, fullLayer, zeroCurv, 1u << 20);
        REQUIRE(r.instances.size() > 0);
        for (const auto& fi : r.instances)
        {
            CHECK((fi.flags & FoliageInstanceFlags::Collider) != 0);
            CHECK((fi.flags & FoliageInstanceFlags::NavContribute) != 0);
        }
    }

    TEST_CASE("budget guard stops generation and flags overflow")
    {
        auto p = oneRuleProfile(0, 1.0f, 1.0f, 0.0f);
        auto pal = onePalette();
        auto r = bakeFoliageScatterForTile(p, pal, 3u, 0, 0, 8.0f,
                                           flatHeight, flatNormal, fullLayer, zeroCurv, /*budget*/ 10);
        CHECK(r.instances.size() == 10);
        CHECK(r.budgetExceeded);
    }

    TEST_CASE("out-of-range palette index places nothing")
    {
        auto p = oneRuleProfile(5, 1.0f, 1.0f, 0.5f); // entry 5, palette has 1 type
        auto pal = onePalette();
        auto r = bakeFoliageScatterForTile(p, pal, 1u, 0, 0, 8.0f,
                                           flatHeight, flatNormal, fullLayer, zeroCurv, 1u << 20);
        CHECK(r.instances.empty());
    }

    TEST_CASE("curvature gate restricts placement to the band (shared evaluator)")
    {
        auto p = oneRuleProfile(0, 1.0f, 1.0f, 0.0f);
        p.rules[0].useCurvatureMask = true;
        p.rules[0].curvatureMin = 0.5f;
        p.rules[0].curvatureMax = 10.0f;
        auto pal = onePalette();
        auto leftConcave = [](float lx, float) { return lx < 4.0f ? 1.0f : -1.0f; };

        auto r = bakeFoliageScatterForTile(p, pal, 11u, 0, 0, 8.0f,
                                           flatHeight, flatNormal, fullLayer, leftConcave, 1u << 20);
        REQUIRE(!r.instances.empty());
        for (const auto& fi : r.instances)
            CHECK(fi.position.x < 4.0f);
    }
}
