#include <doctest.h>
#include <eqs/EQSTypes.hpp>
#include <eqs/generators/EntitiesGenerator.hpp>
#include <eqs/generators/GridGenerator.hpp>
#include <eqs/generators/NavMeshPointsGenerator.hpp>
#include <eqs/generators/RingGenerator.hpp>
#include <eqs/tests/DistanceTest.hpp>
#include <eqs/tests/DotProductTest.hpp>
#include <eqs/tests/OnNavMeshTest.hpp>
#include <eqs/tests/PathfindingCostTest.hpp>
#include <eqs/tests/TraceTest.hpp>
#include <glm/glm.hpp>
#include <cmath>
#include <initializer_list>
#include <optional>
#include <string>
#include <vector>

// ============================================================
// VK-1660 (rewrite of the VK-1101 suite): EQS value types, generators and test metrics
// ============================================================

namespace
{
    eqs::EQSContext querierAt(const glm::vec3& position, uint64_t entity = 7)
    {
        eqs::EQSContext context;
        context.querierEntityId = entity;
        context.querierPosition = position;
        return context;
    }

    eqs::EQSContext withTarget(eqs::EQSContext context, const glm::vec3& target, uint64_t entity = eqs::kNoEntity)
    {
        context.hasTarget = true;
        context.targetPosition = target;
        context.targetEntityId = entity;
        return context;
    }

    std::vector<eqs::EQSCandidate> pointsAt(std::initializer_list<glm::vec3> positions)
    {
        std::vector<eqs::EQSCandidate> candidates;
        for (const glm::vec3& position : positions)
        {
            eqs::EQSCandidate candidate;
            candidate.position = position;
            candidates.push_back(candidate);
        }
        return candidates;
    }

    std::vector<float> measure(const eqs::IEQSTest& test, const std::vector<eqs::EQSCandidate>& candidates,
                               const eqs::EQSContext& context, const eqs::EQSProviderRefs& providers = {})
    {
        std::vector<float> metrics(candidates.size(), -1.0f);
        std::string error;
        REQUIRE(test.evaluate(candidates, context, providers, metrics, error));
        CHECK(error.empty());
        return metrics;
    }

    bool evaluateFails(const eqs::IEQSTest& test, const eqs::EQSContext& context, const eqs::EQSProviderRefs& providers,
                       const std::string& expected)
    {
        auto candidates = pointsAt({glm::vec3(1.0f, 0.0f, 0.0f)});
        std::vector<float> metrics(candidates.size(), 0.0f);
        std::string error;
        const bool ok = test.evaluate(candidates, context, providers, metrics, error);
        return !ok && error.find(expected) != std::string::npos;
    }
}

TEST_SUITE("AI_EQS")
{
    TEST_CASE("EQSQueryHandle: default is invalid; handles compare by id")
    {
        eqs::EQSQueryHandle handle;
        CHECK_FALSE(handle.isValid());
        eqs::EQSQueryHandle a{42};
        eqs::EQSQueryHandle b{42};
        eqs::EQSQueryHandle c{43};
        CHECK(a.isValid());
        CHECK(a == b);
        CHECK(a != c);
    }

    TEST_CASE("EQSResult: best position, entity and score; results need Completed")
    {
        eqs::EQSResult result;
        CHECK_FALSE(result.hasResults());
        CHECK(result.getBestEntity() == eqs::kNoEntity);
        CHECK(result.getBestPosition() == glm::vec3(0.0f));

        eqs::EQSCandidate best;
        best.position = glm::vec3(1.0f, 2.0f, 3.0f);
        best.entityId = 12;
        best.totalScore = 0.75f;
        result.candidates.push_back(best);
        CHECK_FALSE(result.hasResults()); // still Pending

        result.status = eqs::EQSQueryStatus::Completed;
        CHECK(result.hasResults());
        CHECK(result.getBestPosition() == glm::vec3(1.0f, 2.0f, 3.0f));
        CHECK(result.getBestEntity() == 12);
        CHECK(result.getBestScore() == doctest::Approx(0.75f));
    }

    TEST_CASE("contexts and candidates default to no entity (entity 0 is a real entity)")
    {
        CHECK(eqs::kNoEntity == services::EntityHandle::INVALID_ID);
        eqs::EQSContext context;
        CHECK(context.querierEntityId == eqs::kNoEntity);
        CHECK(context.targetEntityId == eqs::kNoEntity);
        CHECK_FALSE(context.hasTarget);
        eqs::EQSCandidate candidate;
        CHECK(candidate.entityId == eqs::kNoEntity);
        CHECK(std::isnan(eqs::kNoMetric));
    }

    TEST_CASE("Ring: pointCount x ringCount points on circles around the querier, at its height")
    {
        eqs::RingGenerator::Settings settings;
        settings.radius = 5.0f;
        settings.pointCount = 8;
        settings.ringCount = 2;
        settings.ringSpacing = 3.0f;
        const eqs::RingGenerator ring(settings);
        CHECK(ring.getName() == "Ring");

        const glm::vec3 center(10.0f, 2.0f, -4.0f);
        std::vector<eqs::EQSCandidate> out;
        std::string error;
        REQUIRE(ring.generate(querierAt(center), {}, out, error));
        REQUIRE(out.size() == 16);
        for (size_t i = 0; i < out.size(); ++i)
        {
            INFO("point ", i);
            const glm::vec3 delta = out[i].position - center;
            CHECK(delta.y == doctest::Approx(0.0f));
            CHECK(glm::length(glm::vec2(delta.x, delta.z)) == doctest::Approx(i < 8 ? 5.0f : 8.0f).epsilon(1e-4));
            CHECK(out[i].entityId == eqs::kNoEntity);
        }
    }

    TEST_CASE("Ring around the target; without a target it fails")
    {
        eqs::RingGenerator::Settings settings;
        settings.center = eqs::EQSContextPoint::Target;
        settings.radius = 2.0f;
        settings.pointCount = 4;
        const eqs::RingGenerator ring(settings);

        std::vector<eqs::EQSCandidate> out;
        std::string error;
        CHECK_FALSE(ring.generate(querierAt(glm::vec3(0.0f)), {}, out, error));
        CHECK(error.find("needs a target") != std::string::npos);

        out.clear();
        error.clear();
        const glm::vec3 target(20.0f, 1.0f, 0.0f);
        REQUIRE(ring.generate(withTarget(querierAt(glm::vec3(0.0f)), target), {}, out, error));
        REQUIRE(out.size() == 4);
        for (const auto& candidate : out)
            CHECK(glm::distance(candidate.position, target) == doctest::Approx(2.0f).epsilon(1e-4));
    }

    TEST_CASE("Grid: size x size points centered on the center")
    {
        eqs::GridGenerator::Settings settings;
        settings.size = 3;
        settings.spacing = 2.0f;
        const eqs::GridGenerator grid(settings);
        CHECK(grid.getName() == "Grid");

        const glm::vec3 center(5.0f, 1.0f, 5.0f);
        std::vector<eqs::EQSCandidate> out;
        std::string error;
        REQUIRE(grid.generate(querierAt(center), {}, out, error));
        REQUIRE(out.size() == 9);

        glm::vec3 sum(0.0f);
        float lowX = out[0].position.x;
        float highX = out[0].position.x;
        for (const auto& candidate : out)
        {
            sum += candidate.position;
            lowX = candidate.position.x < lowX ? candidate.position.x : lowX;
            highX = candidate.position.x > highX ? candidate.position.x : highX;
            CHECK(candidate.position.y == doctest::Approx(1.0f));
        }
        const glm::vec3 centroid = sum / 9.0f;
        CHECK(centroid.x == doctest::Approx(5.0f));
        CHECK(centroid.z == doctest::Approx(5.0f));
        CHECK(lowX == doctest::Approx(3.0f));
        CHECK(highX == doctest::Approx(7.0f));
    }

    TEST_CASE("NavMeshPoints: projected onto the navmesh, the same for the same seed, unprojectable samples dropped")
    {
        int calls = 0;
        eqs::EQSProviderRefs providers;
        providers.projectToNavmesh = [&calls](const glm::vec3& point, float snapRadius) -> std::optional<glm::vec3>
        {
            ++calls;
            CHECK(snapRadius == doctest::Approx(1.5f));
            if (point.x < 0.0f)
                return std::nullopt; // no navmesh on that side
            return glm::vec3(point.x, 0.25f, point.z);
        };

        eqs::NavMeshPointsGenerator::Settings settings;
        settings.radius = 10.0f;
        settings.pointCount = 64;
        settings.snapRadius = 1.5f;
        const eqs::NavMeshPointsGenerator generator(settings);
        CHECK(generator.getName() == "NavMeshPoints");

        eqs::EQSContext context = querierAt(glm::vec3(0.0f));
        context.seed = 1234;
        std::vector<eqs::EQSCandidate> first;
        std::string error;
        REQUIRE(generator.generate(context, providers, first, error));
        CHECK(calls == 64);
        CHECK_FALSE(first.empty());
        CHECK(first.size() < 64);
        for (const auto& candidate : first)
        {
            CHECK(candidate.position.y == doctest::Approx(0.25f));
            CHECK(candidate.position.x >= 0.0f);
            CHECK(glm::length(glm::vec2(candidate.position.x, candidate.position.z)) <= 10.0f + 1e-3f);
        }

        std::vector<eqs::EQSCandidate> again;
        REQUIRE(generator.generate(context, providers, again, error));
        REQUIRE(again.size() == first.size());
        for (size_t i = 0; i < first.size(); ++i)
            CHECK(again[i].position == first[i].position);

        context.seed = 99;
        std::vector<eqs::EQSCandidate> other;
        REQUIRE(generator.generate(context, providers, other, error));
        bool differs = other.size() != first.size();
        for (size_t i = 0; !differs && i < first.size(); ++i)
            differs = other[i].position != first[i].position;
        CHECK(differs);

        std::vector<eqs::EQSCandidate> none;
        CHECK_FALSE(generator.generate(context, {}, none, error));
        CHECK(error.find("projectToNavmesh") != std::string::npos);
    }

    TEST_CASE("Entities: the filter reaches the provider, the querier is excluded, candidates carry the entity")
    {
        eqs::EQSEntityFilter seen;
        eqs::EQSProviderRefs providers;
        providers.findEntities = [&seen](const eqs::EQSEntityFilter& filter)
        {
            seen = filter;
            return std::vector<eqs::EQSEntityCandidate>{
                {7, glm::vec3(1.0f, 0.0f, 0.0f)}, // the querier: a lax provider returned it anyway
                {11, glm::vec3(2.0f, 0.0f, 0.0f)},
                {12, glm::vec3(5.0f, 0.0f, 0.0f)},
            };
        };

        eqs::EntitiesGenerator::Settings settings;
        settings.radius = 30.0f;
        settings.name = "Enemy";
        settings.prefix = true;
        settings.includeInactive = true;
        settings.maxCount = 5;
        const eqs::EntitiesGenerator generator(settings);
        CHECK(generator.getName() == "Entities");

        std::vector<eqs::EQSCandidate> out;
        std::string error;
        REQUIRE(generator.generate(querierAt(glm::vec3(0.0f), 7), providers, out, error));
        CHECK(seen.name == "Enemy");
        CHECK(seen.prefix);
        CHECK(seen.includeInactive);
        CHECK(seen.maxCount == 5);
        CHECK(seen.radius == doctest::Approx(30.0f));
        CHECK(seen.exclude == 7);
        CHECK(seen.center == glm::vec3(0.0f));
        REQUIRE(out.size() == 2);
        CHECK(out[0].entityId == 11);
        CHECK(out[0].position == glm::vec3(2.0f, 0.0f, 0.0f));
        CHECK(out[1].entityId == 12);

        settings.maxCount = 1;
        const eqs::EntitiesGenerator capped(settings);
        out.clear();
        REQUIRE(capped.generate(querierAt(glm::vec3(0.0f), 7), providers, out, error));
        REQUIRE(out.size() == 1);
        CHECK(out[0].entityId == 11);

        out.clear();
        CHECK_FALSE(generator.generate(querierAt(glm::vec3(0.0f)), {}, out, error));
        CHECK(error.find("findEntities") != std::string::npos);
    }

    TEST_CASE("Distance: XZ by default, 3D when horizontal is off, to the target when asked")
    {
        const auto candidates = pointsAt({glm::vec3(3.0f, 10.0f, 4.0f), glm::vec3(0.0f, 0.0f, 1.0f)});
        const eqs::EQSContext context = querierAt(glm::vec3(0.0f));

        const eqs::DistanceTest flat({});
        CHECK(flat.getName() == "Distance");
        auto metrics = measure(flat, candidates, context);
        CHECK(metrics[0] == doctest::Approx(5.0f));
        CHECK(metrics[1] == doctest::Approx(1.0f));

        const eqs::DistanceTest spatial({eqs::EQSContextPoint::Querier, false});
        metrics = measure(spatial, candidates, context);
        CHECK(metrics[0] == doctest::Approx(std::sqrt(125.0f)));

        const eqs::DistanceTest toTarget({eqs::EQSContextPoint::Target, true});
        metrics = measure(toTarget, candidates, withTarget(context, glm::vec3(3.0f, 0.0f, 4.0f)));
        CHECK(metrics[0] == doctest::Approx(0.0f));
        CHECK(evaluateFails(toTarget, context, {}, "needs a target"));
    }

    TEST_CASE("Dot: alignment with the facing, or with the direction to the target, on the XZ plane")
    {
        eqs::EQSContext context = querierAt(glm::vec3(0.0f));
        context.querierForward = glm::vec3(0.0f, 0.0f, 1.0f);
        const auto candidates = pointsAt({
            glm::vec3(0.0f, 0.0f, 5.0f),  // ahead
            glm::vec3(0.0f, 0.0f, -5.0f), // behind
            glm::vec3(5.0f, 0.0f, 0.0f),  // to the side
            glm::vec3(0.0f, 0.0f, 0.0f),  // at the querier
            glm::vec3(0.0f, 10.0f, 5.0f), // ahead and high: height is ignored
        });

        const eqs::DotProductTest forward({});
        CHECK(forward.getName() == "Dot");
        auto metrics = measure(forward, candidates, context);
        CHECK(metrics[0] == doctest::Approx(1.0f));
        CHECK(metrics[1] == doctest::Approx(0.0f));
        CHECK(metrics[2] == doctest::Approx(0.5f));
        CHECK(metrics[3] == doctest::Approx(0.5f));
        CHECK(metrics[4] == doctest::Approx(1.0f));

        const eqs::DotProductTest toTarget({eqs::DotProductTest::Direction::ToTarget});
        metrics = measure(toTarget, candidates, withTarget(context, glm::vec3(5.0f, 0.0f, 0.0f)));
        CHECK(metrics[2] == doctest::Approx(1.0f));
        CHECK(metrics[0] == doctest::Approx(0.5f));
        CHECK(evaluateFails(toTarget, context, {}, "needs a target"));
    }

    TEST_CASE("OnNavMesh: 1 on the navmesh, 0 off it, with the tolerance; needs the provider")
    {
        eqs::EQSProviderRefs providers;
        providers.isPointOnNavmesh = [](const glm::vec3& point, float tolerance)
        {
            CHECK(tolerance == doctest::Approx(0.75f));
            return point.x > 0.0f;
        };
        const eqs::OnNavMeshTest test({0.75f});
        CHECK(test.getName() == "OnNavMesh");
        const auto metrics = measure(test, pointsAt({glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f)}),
                                     querierAt(glm::vec3(0.0f)), providers);
        CHECK(metrics[0] == doctest::Approx(1.0f));
        CHECK(metrics[1] == doctest::Approx(0.0f));
        CHECK(evaluateFails(test, querierAt(glm::vec3(0.0f)), {}, "isPointOnNavmesh"));
    }

    TEST_CASE("PathCost: the path length for the querier's size; no metric when unreachable")
    {
        eqs::EQSProviderRefs providers;
        providers.pathLength = [](const glm::vec3& from, const glm::vec3& to, float radius, float height)
        {
            CHECK(radius == doctest::Approx(0.6f));
            CHECK(height == doctest::Approx(1.8f));
            return to.x < 0.0f ? -1.0f : glm::distance(from, to) * 2.0f;
        };
        eqs::EQSContext context = querierAt(glm::vec3(0.0f));
        context.agentRadius = 0.6f;
        context.agentHeight = 1.8f;

        const eqs::PathfindingCostTest test({});
        CHECK(test.getName() == "PathCost");
        const auto metrics = measure(test, pointsAt({glm::vec3(3.0f, 0.0f, 0.0f), glm::vec3(-3.0f, 0.0f, 0.0f)}),
                                     context, providers);
        CHECK(metrics[0] == doctest::Approx(6.0f));
        CHECK(std::isnan(metrics[1]));

        const eqs::PathfindingCostTest fromTarget({eqs::EQSContextPoint::Target});
        CHECK(evaluateFails(fromTarget, context, providers, "needs a target"));
        CHECK(evaluateFails(test, context, {}, "pathLength"));
    }

    TEST_CASE("Trace: a segment from the eye to the candidate, ignoring the looker and the candidate's entity")
    {
        struct Call
        {
            glm::vec3 from;
            glm::vec3 to;
            uint64_t ignoreA;
            uint64_t ignoreB;
        };
        std::vector<Call> calls;
        eqs::EQSProviderRefs providers;
        providers.lineOfSight = [&calls](const glm::vec3& from, const glm::vec3& to, uint64_t ignoreA, uint64_t ignoreB)
        {
            calls.push_back(Call{from, to, ignoreA, ignoreB});
            return to.x > 0.0f;
        };

        auto candidates = pointsAt({glm::vec3(4.0f, 0.0f, 0.0f), glm::vec3(-4.0f, 0.0f, 0.0f)});
        candidates[0].entityId = 21;

        const eqs::TraceTest test({eqs::EQSContextPoint::Querier, 1.5f, 0.5f});
        CHECK(test.getName() == "Trace");
        const auto metrics = measure(test, candidates, querierAt(glm::vec3(0.0f), 7), providers);
        CHECK(metrics[0] == doctest::Approx(1.0f));
        CHECK(metrics[1] == doctest::Approx(0.0f));
        REQUIRE(calls.size() == 2);
        CHECK(calls[0].from == glm::vec3(0.0f, 1.5f, 0.0f));
        CHECK(calls[0].to == glm::vec3(4.0f, 0.5f, 0.0f));
        CHECK(calls[0].ignoreA == 7);
        CHECK(calls[0].ignoreB == 21);
        CHECK(calls[1].ignoreB == eqs::kNoEntity);

        calls.clear();
        const eqs::TraceTest fromTarget({eqs::EQSContextPoint::Target, 1.5f, 0.5f});
        measure(fromTarget, candidates, withTarget(querierAt(glm::vec3(0.0f), 7), glm::vec3(10.0f, 0.0f, 0.0f), 33),
                providers);
        REQUIRE(calls.size() == 2);
        CHECK(calls[0].from == glm::vec3(10.0f, 1.5f, 0.0f));
        CHECK(calls[0].ignoreA == 33);

        CHECK(evaluateFails(test, querierAt(glm::vec3(0.0f)), {}, "lineOfSight"));
    }
}
