#include <doctest.h>
#include "navigation/FormationSlots.hpp"
#include <glm/gtc/constants.hpp>

namespace
{
    bool closeVec(const glm::vec3& a, const glm::vec3& b, float eps = 0.0001f)
    {
        return glm::length(a - b) <= eps;
    }
}

TEST_SUITE("FormationSlots")
{
    TEST_CASE("grid ports the RTSDemo centered square math")
    {
        CHECK(closeVec(navigation::formationSlotLocal(navigation::FormationKind::Grid, 0, 1, 2.0f),
                       {0.0f, 0.0f, 0.0f}));

        CHECK(closeVec(navigation::formationSlotLocal(navigation::FormationKind::Grid, 0, 9, 2.0f),
                       {-2.0f, 0.0f, -2.0f}));
        CHECK(closeVec(navigation::formationSlotLocal(navigation::FormationKind::Grid, 4, 9, 2.0f),
                       {0.0f, 0.0f, 0.0f}));
        CHECK(closeVec(navigation::formationSlotLocal(navigation::FormationKind::Grid, 8, 9, 2.0f),
                       {2.0f, 0.0f, 2.0f}));

        CHECK(closeVec(navigation::formationSlotLocal(navigation::FormationKind::Grid, 0, 5, 2.0f),
                       {-2.0f, 0.0f, -1.0f}));
        CHECK(closeVec(navigation::formationSlotLocal(navigation::FormationKind::Grid, 4, 5, 2.0f),
                       {0.0f, 0.0f, 1.0f}));
    }

    TEST_CASE("other formation kinds produce deterministic local slots")
    {
        CHECK(closeVec(navigation::formationSlotLocal(navigation::FormationKind::Line, 0, 3, 2.0f),
                       {-2.0f, 0.0f, 0.0f}));
        CHECK(closeVec(navigation::formationSlotLocal(navigation::FormationKind::Column, 2, 3, 2.0f),
                       {0.0f, 0.0f, 2.0f}));
        CHECK(closeVec(navigation::formationSlotLocal(navigation::FormationKind::Wedge, 0, 5, 2.0f),
                       {0.0f, 0.0f, 0.0f}));
        CHECK(closeVec(navigation::formationSlotLocal(navigation::FormationKind::Box, 0, 9, 2.0f),
                       {-2.0f, 0.0f, -2.0f}));
    }

    TEST_CASE("toWorld rotates local slots around Y")
    {
        const glm::vec3 center{10.0f, 3.0f, 20.0f};
        CHECK(closeVec(navigation::toWorld({0.0f, 0.0f, 2.0f}, center, 0.0f),
                       {10.0f, 3.0f, 22.0f}));
        CHECK(closeVec(navigation::toWorld({0.0f, 0.0f, 2.0f}, center, glm::half_pi<float>()),
                       {12.0f, 3.0f, 20.0f}));
        CHECK(closeVec(navigation::toWorld({2.0f, 0.0f, 0.0f}, center, glm::half_pi<float>()),
                       {10.0f, 3.0f, 18.0f}));
    }

    TEST_CASE("stable assignment preserves ordering under small shifts")
    {
        const glm::vec3 center{0.0f};
        navigation::FormationParams params;
        params.kind = navigation::FormationKind::Grid;
        params.spacing = 2.0f;

        const glm::vec3 unitsA[] = {
            {-2.1f, 0.0f, -1.9f},
            {0.1f, 0.0f, -2.0f},
            {-2.0f, 0.0f, 0.1f},
            {0.0f, 0.0f, 0.0f}
        };
        const glm::vec3 unitsB[] = {
            {-2.0f, 0.0f, -1.8f},
            {0.2f, 0.0f, -1.9f},
            {-1.9f, 0.0f, 0.2f},
            {0.1f, 0.0f, 0.1f}
        };

        int slotsA[4] = {-1, -1, -1, -1};
        int slotsB[4] = {-1, -1, -1, -1};
        navigation::assignSlotsStable(unitsA, center, 0.0f, params, slotsA);
        navigation::assignSlotsStable(unitsB, center, 0.0f, params, slotsB);

        for (int i = 0; i < 4; ++i)
            CHECK(slotsA[i] == slotsB[i]);
    }
}
