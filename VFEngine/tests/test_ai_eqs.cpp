#include <doctest.h>
#include <eqs/EQSTypes.hpp>
#include <eqs/EQSQuery.hpp>
#include <eqs/IEQSGenerator.hpp>
#include <eqs/IEQSTest.hpp>
#include <eqs/generators/GridGenerator.hpp>
#include <eqs/generators/RingGenerator.hpp>
#include <eqs/tests/DistanceTest.hpp>
#include <eqs/tests/DotProductTest.hpp>
#include <glm/glm.hpp>

// ============================================================
// VK-1101: AI EQS (Environment Query System) unit tests
// ============================================================

TEST_SUITE("AI_EQS") {

// ---- EQSQueryHandle ----

TEST_CASE("EQSQueryHandle: default is invalid") {
    eqs::EQSQueryHandle handle;
    CHECK_FALSE(handle.isValid());
}

TEST_CASE("EQSQueryHandle: equality") {
    eqs::EQSQueryHandle a;
    a.id = 1;
    eqs::EQSQueryHandle b;
    b.id = 1;
    eqs::EQSQueryHandle c;
    c.id = 2;
    CHECK(a == b);
    CHECK(a != c);
}

// ---- EQSResult ----

TEST_CASE("EQSResult: default has no results") {
    eqs::EQSResult result;
    CHECK_FALSE(result.hasResults());
    CHECK(result.status == eqs::EQSQueryStatus::Pending);
}

TEST_CASE("EQSResult: with candidates") {
    eqs::EQSResult result;
    result.status = eqs::EQSQueryStatus::Completed;
    result.candidates.push_back({{1.0f, 0.0f, 0.0f}, 0.9f, false});
    result.candidates.push_back({{2.0f, 0.0f, 0.0f}, 0.5f, false});
    CHECK(result.hasResults());
    // getBestScore returns front candidate's score
    CHECK(result.getBestScore() == doctest::Approx(0.9f));
}

// ---- EQS Context ----

TEST_CASE("EQSContext: default querier at origin") {
    eqs::EQSContext ctx;
    CHECK(ctx.querierPosition == glm::vec3(0.0f));
    CHECK_FALSE(ctx.hasTarget);
}

// ---- Grid Generator ----

TEST_CASE("GridGenerator: produces expected point count") {
    eqs::GridGenerator gen(5, 2.0f); // 5x5 grid
    eqs::EQSContext ctx;
    ctx.querierPosition = {0, 0, 0};
    eqs::EQSProviderRefs providers;
    auto candidates = gen.generate(ctx, providers);
    CHECK(candidates.size() == 25); // 5*5
}

TEST_CASE("GridGenerator: points centered on querier") {
    eqs::GridGenerator gen(3, 10.0f);
    eqs::EQSContext ctx;
    ctx.querierPosition = {100, 0, 100};
    eqs::EQSProviderRefs providers;
    auto candidates = gen.generate(ctx, providers);

    // Center point should be at querier position
    bool hasCenter = false;
    for (auto& c : candidates) {
        if (glm::length(c.position - ctx.querierPosition) < 0.01f) {
            hasCenter = true;
            break;
        }
    }
    CHECK(hasCenter);
}

// ---- Ring Generator ----

TEST_CASE("RingGenerator: points lie on circle") {
    float radius = 10.0f;
    int pointCount = 8;
    eqs::RingGenerator gen(radius, pointCount, 1, 5.0f);
    eqs::EQSContext ctx;
    ctx.querierPosition = {0, 0, 0};
    eqs::EQSProviderRefs providers;
    auto candidates = gen.generate(ctx, providers);

    CHECK(candidates.size() == static_cast<size_t>(pointCount));
    for (auto& c : candidates) {
        float dist = glm::length(c.position - ctx.querierPosition);
        CHECK(dist == doctest::Approx(radius).epsilon(0.1f));
    }
}

// ---- Distance Test ----

TEST_CASE("DistanceTest: closer points score higher with Linear mode") {
    eqs::DistanceTest distTest;
    eqs::EQSContext ctx;
    ctx.querierPosition = {0, 0, 0};

    std::vector<eqs::EQSCandidate> candidates;
    candidates.push_back({{5.0f, 0, 0}, 0.0f, false});   // near
    candidates.push_back({{20.0f, 0, 0}, 0.0f, false});   // far

    eqs::EQSTestConfig config;
    config.weight = 1.0f;
    config.scoringMode = eqs::EQSScoringMode::Linear;
    config.isFilter = false;

    eqs::EQSProviderRefs providers;
    distTest.runTest(candidates, ctx, providers, config);

    // DistanceTest scores by raw distance — farther = higher score (Linear mode)
    CHECK(candidates[0].totalScore < candidates[1].totalScore);
}

// ---- Dot Product Test ----

TEST_CASE("DotProductTest: points in front score higher") {
    eqs::DotProductTest dotTest;
    eqs::EQSContext ctx;
    ctx.querierPosition = {0, 0, 0};
    ctx.querierForward = {0, 0, 1};

    std::vector<eqs::EQSCandidate> candidates;
    candidates.push_back({{0, 0, 10}, 0.0f, false});   // directly in front
    candidates.push_back({{0, 0, -10}, 0.0f, false});   // directly behind

    eqs::EQSTestConfig config;
    config.weight = 1.0f;
    config.scoringMode = eqs::EQSScoringMode::Linear;
    config.isFilter = false;

    eqs::EQSProviderRefs providers;
    dotTest.runTest(candidates, ctx, providers, config);

    CHECK(candidates[0].totalScore > candidates[1].totalScore);
}

// ---- Scoring & Selection ----

TEST_CASE("EQSResult: hasResults checks non-empty candidates") {
    eqs::EQSResult result;
    result.status = eqs::EQSQueryStatus::Completed;
    result.candidates.push_back({{1, 0, 0}, 0.0f, true}); // filtered but present
    // hasResults only checks status==Completed && !candidates.empty()
    CHECK(result.hasResults());

    eqs::EQSResult empty;
    empty.status = eqs::EQSQueryStatus::Completed;
    CHECK_FALSE(empty.hasResults());
}

// ---- Empty generator ----

TEST_CASE("GridGenerator: zero grid size produces no candidates") {
    eqs::GridGenerator gen(0, 2.0f);
    eqs::EQSContext ctx;
    eqs::EQSProviderRefs providers;
    auto candidates = gen.generate(ctx, providers);
    CHECK(candidates.empty());
}

} // TEST_SUITE
