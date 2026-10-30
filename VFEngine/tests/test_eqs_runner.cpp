#include <doctest.h>
#include <eqs/EQSQuery.hpp>
#include <eqs/EQSQueryRunner.hpp>
#include <eqs/EQSQuerySpec.hpp>
#include <eqs/EQSTypes.hpp>
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

// ============================================================
// VK-1660: eqs::runQuery - filters before scorers, normalisation and scoring modes, no metric leaking
// between tests, stable ordering, failures, debug detail, and spec-built queries end to end.
// ============================================================

namespace
{
    eqs::EQSCandidate at(float x, float z = 0.0f, uint64_t entity = eqs::kNoEntity)
    {
        eqs::EQSCandidate candidate;
        candidate.position = glm::vec3(x, 0.0f, z);
        candidate.entityId = entity;
        return candidate;
    }

    class FixedGenerator : public eqs::IEQSGenerator
    {
    public:
        explicit FixedGenerator(std::vector<eqs::EQSCandidate> points)
            : points(std::move(points))
        {
        }

        bool generate(const eqs::EQSContext&, const eqs::EQSProviderRefs&, std::vector<eqs::EQSCandidate>& out,
                      std::string&) const override
        {
            out.insert(out.end(), points.begin(), points.end());
            return true;
        }

        std::string getName() const override { return "Fixed"; }

    private:
        std::vector<eqs::EQSCandidate> points;
    };

    enum class Axis
    {
        X,
        Z
    };

    // Metric = the candidate's x (or z); kNoMetric for a negative value when `noMetricBelowZero`.
    class AxisTest : public eqs::IEQSTest
    {
    public:
        AxisTest(std::string name, Axis axis, bool noMetricBelowZero = false)
            : name(std::move(name)), axis(axis), noMetricBelowZero(noMetricBelowZero)
        {
        }

        bool evaluate(std::span<const eqs::EQSCandidate> candidates, const eqs::EQSContext&,
                      const eqs::EQSProviderRefs&, std::span<float> metrics, std::string&) const override
        {
            lastCount = candidates.size();
            for (size_t i = 0; i < candidates.size(); ++i)
            {
                const float value = axis == Axis::X ? candidates[i].position.x : candidates[i].position.z;
                metrics[i] = (noMetricBelowZero && value < 0.0f) ? eqs::kNoMetric : value;
            }
            return true;
        }

        std::string getName() const override { return name; }

        mutable size_t lastCount = 0;

    private:
        std::string name;
        Axis axis;
        bool noMetricBelowZero;
    };

    class ThrowingTest : public eqs::IEQSTest
    {
    public:
        bool evaluate(std::span<const eqs::EQSCandidate>, const eqs::EQSContext&, const eqs::EQSProviderRefs&,
                      std::span<float>, std::string&) const override
        {
            throw std::runtime_error("boom");
        }

        std::string getName() const override { return "Throwing"; }
    };

    class RefusingTest : public eqs::IEQSTest
    {
    public:
        bool evaluate(std::span<const eqs::EQSCandidate>, const eqs::EQSContext&, const eqs::EQSProviderRefs&,
                      std::span<float>, std::string& error) const override
        {
            error = "the Refusing test needs a provider";
            return false;
        }

        std::string getName() const override { return "Refusing"; }
    };

    eqs::EQSTestConfig scoring(eqs::EQSScoringMode mode, float weight = 1.0f,
                               eqs::EQSScoreCombine combine = eqs::EQSScoreCombine::Multiply)
    {
        eqs::EQSTestConfig config;
        config.purpose = eqs::EQSTestPurpose::Score;
        config.mode = mode;
        config.weight = weight;
        config.combine = combine;
        return config;
    }

    eqs::EQSTestConfig filtering(std::optional<float> low, std::optional<float> high)
    {
        eqs::EQSTestConfig config;
        config.purpose = eqs::EQSTestPurpose::Filter;
        config.filterMin = low;
        config.filterMax = high;
        return config;
    }

    eqs::EQSQueryDef makeQuery(std::vector<eqs::EQSCandidate> points, std::vector<eqs::EQSTestEntry> tests,
                               uint32_t maxResults)
    {
        eqs::EQSQueryDef def;
        def.name = "Test";
        def.generator = std::make_shared<FixedGenerator>(std::move(points));
        def.tests = std::move(tests);
        def.maxResults = maxResults;
        return def;
    }

    eqs::EQSTestEntry entry(std::shared_ptr<const eqs::IEQSTest> test, eqs::EQSTestConfig config)
    {
        return eqs::EQSTestEntry{std::move(test), config};
    }

    std::shared_ptr<AxisTest> axisTest(const char* name, Axis axis, bool noMetricBelowZero = false)
    {
        return std::make_shared<AxisTest>(name, axis, noMetricBelowZero);
    }

    eqs::EQSResult run(const eqs::EQSQueryDef& def, eqs::EQSDebugResult* debug = nullptr)
    {
        return eqs::runQuery(def, eqs::EQSContext{}, eqs::EQSProviderRefs{}, debug);
    }
}

TEST_SUITE("EQSQueryRunner")
{
    TEST_CASE("InverseLinear puts the lowest metric first, Linear the highest")
    {
        const auto query = [](eqs::EQSScoringMode mode)
        {
            return makeQuery({at(5.0f), at(1.0f), at(3.0f)}, {entry(axisTest("X", Axis::X), scoring(mode))}, 3);
        };

        const auto nearest = run(query(eqs::EQSScoringMode::InverseLinear));
        REQUIRE(nearest.status == eqs::EQSQueryStatus::Completed);
        REQUIRE(nearest.candidates.size() == 3);
        CHECK(nearest.candidates[0].position.x == doctest::Approx(1.0f));
        CHECK(nearest.candidates[0].totalScore == doctest::Approx(1.0f));
        CHECK(nearest.candidates[1].position.x == doctest::Approx(3.0f));
        CHECK(nearest.candidates[1].totalScore == doctest::Approx(0.5f));
        CHECK(nearest.candidates[2].position.x == doctest::Approx(5.0f));
        CHECK(nearest.candidates[2].totalScore == doctest::Approx(0.0f));

        const auto farthest = run(query(eqs::EQSScoringMode::Linear));
        CHECK(farthest.candidates[0].position.x == doctest::Approx(5.0f));

        const auto squared = run(query(eqs::EQSScoringMode::Square));
        CHECK(squared.candidates[1].totalScore == doctest::Approx(0.25f));
    }

    TEST_CASE("a test's metric never leaks into another test's score")
    {
        // X favours B, Z favours A; only C scores in both.
        const auto def = makeQuery({at(0.0f, 10.0f), at(10.0f, 0.0f), at(5.0f, 5.0f)},
                                   {entry(axisTest("X", Axis::X), scoring(eqs::EQSScoringMode::Linear)),
                                    entry(axisTest("Z", Axis::Z), scoring(eqs::EQSScoringMode::Linear))},
                                   3);
        const auto result = run(def);
        REQUIRE(result.candidates.size() == 3);
        CHECK(result.candidates[0].position.x == doctest::Approx(5.0f));
        CHECK(result.candidates[0].totalScore == doctest::Approx(0.25f));
        CHECK(result.candidates[1].totalScore == doctest::Approx(0.0f));
    }

    TEST_CASE("filters run before scorers, whatever order they are declared in")
    {
        const auto scorer = axisTest("X", Axis::X);
        const auto def = makeQuery({at(0.0f, 0.0f), at(10.0f, 0.0f), at(100.0f, 9.0f)},
                                   {entry(scorer, scoring(eqs::EQSScoringMode::Linear)),
                                    entry(axisTest("Z", Axis::Z), filtering(std::nullopt, 4.0f))},
                                   3);
        const auto result = run(def);
        REQUIRE(result.candidates.size() == 2);
        CHECK(scorer->lastCount == 2); // the filtered candidate was never scored
        CHECK(result.candidates[0].position.x == doctest::Approx(10.0f));
        CHECK(result.candidates[0].totalScore == doctest::Approx(1.0f)); // normalised over 0..10, not 0..100
    }

    TEST_CASE("filters drop candidates outside their bounds or without a metric")
    {
        const auto def = makeQuery({at(-1.0f), at(1.0f), at(3.0f), at(5.0f)},
                                   {entry(axisTest("Reachable", Axis::X, true), filtering(std::nullopt, std::nullopt)),
                                    entry(axisTest("X", Axis::X), filtering(2.0f, 4.0f))},
                                   10);
        const auto result = run(def);
        REQUIRE(result.candidates.size() == 1);
        CHECK(result.candidates[0].position.x == doctest::Approx(3.0f));
    }

    TEST_CASE("a candidate without a metric gets the worst score (0)")
    {
        const auto def = makeQuery({at(-1.0f), at(2.0f), at(4.0f)},
                                   {entry(axisTest("Path", Axis::X, true),
                                          scoring(eqs::EQSScoringMode::InverseLinear, 1.0f, eqs::EQSScoreCombine::Additive))},
                                   3);
        const auto result = run(def);
        REQUIRE(result.candidates.size() == 3);
        CHECK(result.candidates[0].position.x == doctest::Approx(2.0f));
        CHECK(result.candidates[0].totalScore == doctest::Approx(2.0f)); // 1 + 1
        CHECK(result.candidates[1].position.x == doctest::Approx(-1.0f)); // ties keep generation order
        CHECK(result.candidates[1].totalScore == doctest::Approx(1.0f));  // 1 + 0
        CHECK(result.candidates[2].totalScore == doctest::Approx(1.0f));
    }

    TEST_CASE("Multiply and Additive combine into a total that starts at 1; weight scales the score")
    {
        const auto multiplied = run(makeQuery({at(0.0f), at(10.0f)},
                                              {entry(axisTest("X", Axis::X), scoring(eqs::EQSScoringMode::Linear, 2.0f))}, 2));
        CHECK(multiplied.candidates[0].totalScore == doctest::Approx(2.0f));
        CHECK(multiplied.candidates[1].totalScore == doctest::Approx(0.0f));

        const auto added = run(makeQuery({at(0.0f), at(10.0f)},
                                         {entry(axisTest("X", Axis::X),
                                                scoring(eqs::EQSScoringMode::Linear, 2.0f, eqs::EQSScoreCombine::Additive))},
                                         2));
        CHECK(added.candidates[0].totalScore == doctest::Approx(3.0f));
        CHECK(added.candidates[1].totalScore == doctest::Approx(1.0f));
    }

    TEST_CASE("a test whose metrics are all equal scores 1 in every mode, so it cannot zero a Multiply chain")
    {
        const auto def = makeQuery({at(5.0f, 0.0f), at(5.0f, 3.0f)},
                                   {entry(axisTest("X", Axis::X), scoring(eqs::EQSScoringMode::InverseLinear)),
                                    entry(axisTest("Z", Axis::Z), scoring(eqs::EQSScoringMode::Linear))},
                                   2);
        const auto result = run(def);
        REQUIRE(result.candidates.size() == 2);
        CHECK(result.candidates[0].position.z == doctest::Approx(3.0f));
        CHECK(result.candidates[0].totalScore == doctest::Approx(1.0f));

        const auto single = run(makeQuery({at(7.0f)},
                                          {entry(axisTest("X", Axis::X), scoring(eqs::EQSScoringMode::InverseLinear, 0.5f))}, 1));
        REQUIRE(single.candidates.size() == 1);
        CHECK(single.candidates[0].totalScore == doctest::Approx(0.5f));
    }

    TEST_CASE("ties keep generation order; maxResults keeps the best ones")
    {
        const auto def = makeQuery({at(3.0f), at(1.0f), at(2.0f)},
                                   {entry(axisTest("X", Axis::X), scoring(eqs::EQSScoringMode::Constant))}, 2);
        const auto result = run(def);
        REQUIRE(result.candidates.size() == 2);
        CHECK(result.candidates[0].position.x == doctest::Approx(3.0f));
        CHECK(result.candidates[1].position.x == doctest::Approx(1.0f));
    }

    TEST_CASE("no survivor is a Completed result with no candidates")
    {
        const auto result = run(makeQuery({at(1.0f), at(2.0f)},
                                          {entry(axisTest("X", Axis::X), filtering(1000.0f, std::nullopt))}, 1));
        CHECK(result.status == eqs::EQSQueryStatus::Completed);
        CHECK(result.candidates.empty());
        CHECK_FALSE(result.hasResults());
    }

    TEST_CASE("throwing or refusing tests, missing parts and too many candidates fail the query")
    {
        const auto thrown = run(makeQuery({at(1.0f)}, {entry(std::make_shared<ThrowingTest>(), scoring(eqs::EQSScoringMode::Linear))}, 1));
        CHECK(thrown.status == eqs::EQSQueryStatus::Failed);
        CHECK(thrown.error.find("boom") != std::string::npos);
        CHECK(thrown.candidates.empty());

        const auto refused = run(makeQuery({at(1.0f)}, {entry(std::make_shared<RefusingTest>(), filtering(std::nullopt, std::nullopt))}, 1));
        CHECK(refused.status == eqs::EQSQueryStatus::Failed);
        CHECK(refused.error.find("needs a provider") != std::string::npos);

        eqs::EQSQueryDef noGenerator;
        CHECK(run(noGenerator).status == eqs::EQSQueryStatus::Failed);

        const auto noTest = run(makeQuery({at(1.0f)}, {entry(nullptr, scoring(eqs::EQSScoringMode::Linear))}, 1));
        CHECK(noTest.status == eqs::EQSQueryStatus::Failed);

        std::vector<eqs::EQSCandidate> tooMany(eqs::kMaxCandidates + 1, at(0.0f));
        const auto overflow = run(makeQuery(std::move(tooMany), {}, 1));
        CHECK(overflow.status == eqs::EQSQueryStatus::Failed);
        CHECK(overflow.error.find("limit") != std::string::npos);
    }

    TEST_CASE("debug detail: every candidate, its metrics and scores, and the filter that dropped it")
    {
        const auto def = makeQuery({at(0.0f, 1.0f), at(2.0f, 3.0f), at(4.0f, 5.0f)},
                                   {entry(axisTest("X", Axis::X), filtering(1.0f, std::nullopt)),
                                    entry(axisTest("Z", Axis::Z), scoring(eqs::EQSScoringMode::Linear))},
                                   1);
        eqs::EQSDebugResult debug;
        const auto result = run(def, &debug);

        CHECK(debug.result.status == result.status);
        CHECK(debug.result.candidates.size() == result.candidates.size());
        CHECK(debug.testNames == std::vector<std::string>{"X", "Z"});
        CHECK(debug.elapsedMs >= 0.0);
        REQUIRE(debug.candidates.size() == 3);

        const auto& dropped = debug.candidates[0];
        CHECK(dropped.filteredByTest == 0);
        CHECK(dropped.metrics[0] == doctest::Approx(0.0f));
        CHECK(std::isnan(dropped.metrics[1])); // never scored
        CHECK(dropped.scores[1] == doctest::Approx(0.0f));
        CHECK(dropped.total == doctest::Approx(0.0f));

        const auto& best = debug.candidates[2];
        CHECK(best.filteredByTest == -1);
        CHECK(best.metrics[0] == doctest::Approx(4.0f));
        CHECK(best.metrics[1] == doctest::Approx(5.0f));
        CHECK(best.scores[0] == doctest::Approx(0.0f)); // filters do not score
        CHECK(best.scores[1] == doctest::Approx(1.0f));
        CHECK(best.total == doctest::Approx(1.0f));
        CHECK(debug.candidates[1].total == doctest::Approx(0.0f)); // kept, lowest Z
    }

    TEST_CASE("a spec-built query runs end to end with fake providers")
    {
        const auto parsed = eqs::parseQuerySpec(nlohmann::json::parse(R"({
            "generator": {"type": "Entities", "params": {"name": "Enemy", "radius": 50}},
            "tests": [{"type": "Distance", "score": {"mode": "InverseLinear"}},
                      {"type": "Trace", "filter": {}}],
            "maxResults": 1
        })"));
        REQUIRE(parsed.spec.has_value());
        const eqs::EQSQueryDef def = eqs::buildQuery(*parsed.spec, "NearestVisibleEnemy");

        eqs::EQSProviderRefs providers;
        providers.findEntities = [](const eqs::EQSEntityFilter& filter)
        {
            CHECK(filter.exclude == 1);
            return std::vector<eqs::EQSEntityCandidate>{
                {20, glm::vec3(8.0f, 0.0f, 0.0f)},
                {21, glm::vec3(-3.0f, 0.0f, 0.0f)}, // nearest, but behind a wall
                {22, glm::vec3(15.0f, 0.0f, 0.0f)},
            };
        };
        providers.lineOfSight = [](const glm::vec3&, const glm::vec3& to, uint64_t ignoreA, uint64_t)
        {
            CHECK(ignoreA == 1);
            return to.x > 0.0f;
        };

        eqs::EQSContext context;
        context.querierEntityId = 1;
        const auto result = eqs::runQuery(def, context, providers);
        REQUIRE(result.hasResults());
        CHECK(result.getBestEntity() == 20);
        CHECK(result.candidates.size() == 1);
    }

    TEST_CASE("a query that needs a target fails without one")
    {
        const auto parsed = eqs::parseQuerySpec(nlohmann::json::parse(R"({
            "generator": {"type": "Ring", "params": {"center": "target"}},
            "tests": [{"type": "Distance", "score": {}}]
        })"));
        REQUIRE(parsed.spec.has_value());
        const auto result = eqs::runQuery(eqs::buildQuery(*parsed.spec, "AroundTarget"), eqs::EQSContext{}, {});
        CHECK(result.status == eqs::EQSQueryStatus::Failed);
        CHECK(result.error.find("needs a target") != std::string::npos);

        eqs::EQSContext withTarget;
        withTarget.hasTarget = true;
        withTarget.targetPosition = glm::vec3(10.0f, 0.0f, 0.0f);
        const auto ok = eqs::runQuery(eqs::buildQuery(*parsed.spec, "AroundTarget"), withTarget, {});
        CHECK(ok.status == eqs::EQSQueryStatus::Completed);
        CHECK(ok.candidates.size() == 1);
    }
}
