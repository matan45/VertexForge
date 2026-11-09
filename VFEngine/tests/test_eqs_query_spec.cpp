#include <doctest.h>
#include <eqs/EQSQuerySpec.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <variant>
#include <vector>
#include "test_repo_scan_helpers.hpp" // last: it includes <windows.h> (min / max / near / far macros)

// ============================================================
// VK-1660: EQS query templates - the catalogue, strict parsing with JSON paths, the canonical form,
// buildQuery / summarizeQuery / isValidQueryName, and the shipped NearestEnemy template.
// ============================================================

namespace
{
    using json = nlohmann::json;

    bool hasError(const eqs::EQSSpecParseResult& result, const std::string& path, const std::string& text = "")
    {
        return std::any_of(result.errors.begin(), result.errors.end(), [&](const eqs::EQSSpecError& error)
        {
            return error.path == path && (text.empty() || error.message.find(text) != std::string::npos);
        });
    }

    json nearestEnemy()
    {
        return json::parse(R"({
            "description": "nearest",
            "generator": {"type": "Entities", "params": {"name": "Enemy", "radius": 25}},
            "tests": [{"type": "Distance", "score": {"mode": "InverseLinear"}}],
            "maxResults": 1
        })");
    }

    json ringWithTest(const json& test, int pointCount, int ringCount)
    {
        json document = json::object();
        document["generator"] = {{"type", "Ring"}, {"params", {{"pointCount", pointCount}, {"ringCount", ringCount}}}};
        document["tests"] = json::array({test});
        return document;
    }

    void checkDefaultsInRange(const std::vector<eqs::EQSParamSpec>& params)
    {
        for (const eqs::EQSParamSpec& param : params)
        {
            INFO("param ", param.name);
            switch (param.kind)
            {
            case eqs::EQSParamKind::Float:
            {
                REQUIRE(std::holds_alternative<float>(param.defaultValue));
                const float value = std::get<float>(param.defaultValue);
                CHECK(param.hasRange);
                CHECK(value >= param.minValue);
                CHECK(value <= param.maxValue);
                break;
            }
            case eqs::EQSParamKind::Int:
            {
                REQUIRE(std::holds_alternative<int32_t>(param.defaultValue));
                const float value = static_cast<float>(std::get<int32_t>(param.defaultValue));
                CHECK(param.hasRange);
                CHECK(value >= param.minValue);
                CHECK(value <= param.maxValue);
                break;
            }
            case eqs::EQSParamKind::Bool:
                CHECK(std::holds_alternative<bool>(param.defaultValue));
                break;
            case eqs::EQSParamKind::Enum:
            {
                REQUIRE(std::holds_alternative<std::string>(param.defaultValue));
                const std::string& value = std::get<std::string>(param.defaultValue);
                CHECK(std::find(param.enumValues.begin(), param.enumValues.end(), value) != param.enumValues.end());
                break;
            }
            case eqs::EQSParamKind::String:
                CHECK(std::holds_alternative<std::string>(param.defaultValue));
                break;
            }
        }
    }
}

TEST_SUITE("EQSQuerySpec")
{
    TEST_CASE("the catalogue lists every generator and test type; defaults lie in their ranges")
    {
        std::vector<std::string> generators;
        for (const auto& type : eqs::generatorTypes())
        {
            generators.push_back(type.type);
            checkDefaultsInRange(type.params);
        }
        CHECK(generators == std::vector<std::string>{"Ring", "Grid", "NavMeshPoints", "Entities"});

        std::vector<std::string> tests;
        for (const auto& type : eqs::testTypes())
        {
            tests.push_back(type.type);
            checkDefaultsInRange(type.params);
            CHECK(type.boolean == (type.type == "OnNavMesh" || type.type == "Trace"));
            CHECK_FALSE(type.metric.empty());
        }
        CHECK(tests == std::vector<std::string>{"Distance", "Dot", "OnNavMesh", "PathCost", "Trace"});

        REQUIRE(eqs::findGeneratorType("ring") != nullptr);
        CHECK(eqs::findGeneratorType("ring")->type == "Ring");
        REQUIRE(eqs::findTestType("TRACE") != nullptr);
        CHECK(eqs::findTestType("TRACE")->type == "Trace");
        CHECK(eqs::findGeneratorType("Cone") == nullptr);
        CHECK(eqs::findTestType("Cone") == nullptr);
    }

    TEST_CASE("parsing fills omitted params with defaults; types and enums are case-insensitive")
    {
        json document = nearestEnemy();
        document["generator"]["type"] = "entities";
        document["generator"]["params"]["match"] = "PREFIX";
        const auto result = eqs::parseQuerySpec(document);
        REQUIRE(result.errors.empty());
        REQUIRE(result.spec.has_value());

        const auto& generator = result.spec->generator;
        CHECK(generator.type == "Entities");
        CHECK(std::get<std::string>(generator.params.at("match")) == "prefix");
        CHECK(std::get<std::string>(generator.params.at("center")) == "querier");
        CHECK(std::get<float>(generator.params.at("radius")) == doctest::Approx(25.0f)); // a JSON integer for a Float
        CHECK(std::get<int32_t>(generator.params.at("maxCount")) == 64);
        CHECK(std::get<bool>(generator.params.at("includeInactive")) == false);
        CHECK(std::get<std::string>(generator.params.at("name")) == "Enemy");

        REQUIRE(result.spec->tests.size() == 1);
        const auto& test = result.spec->tests[0];
        CHECK(test.type == "Distance");
        CHECK(test.config.purpose == eqs::EQSTestPurpose::Score);
        CHECK(test.config.mode == eqs::EQSScoringMode::InverseLinear);
        CHECK(test.config.weight == doctest::Approx(1.0f));
        CHECK(test.config.combine == eqs::EQSScoreCombine::Multiply);
        CHECK(std::get<bool>(test.params.at("horizontal")) == true);
        CHECK(std::get<std::string>(test.params.at("to")) == "querier");
        CHECK(result.spec->maxResults == 1);
        CHECK(result.spec->description == "nearest");
    }

    TEST_CASE("strict errors carry their JSON path")
    {
        SUBCASE("not an object")
        {
            CHECK(hasError(eqs::parseQuerySpec(json::array()), "", "must be a JSON object"));
        }
        SUBCASE("unknown top-level key")
        {
            json document = nearestEnemy();
            document["extra"] = 1;
            CHECK(hasError(eqs::parseQuerySpec(document), "extra", "unknown key"));
        }
        SUBCASE("version other than 1")
        {
            json document = nearestEnemy();
            document["version"] = 2;
            CHECK(hasError(eqs::parseQuerySpec(document), "version", "must be 1"));
        }
        SUBCASE("missing generator")
        {
            json document = nearestEnemy();
            document.erase("generator");
            CHECK(hasError(eqs::parseQuerySpec(document), "generator", "missing"));
        }
        SUBCASE("unknown generator type")
        {
            json document = nearestEnemy();
            document["generator"]["type"] = "Cone";
            CHECK(hasError(eqs::parseQuerySpec(document), "generator.type", "unknown generator type 'Cone'"));
        }
        SUBCASE("unknown param")
        {
            json document = nearestEnemy();
            document["generator"]["params"]["speed"] = 3;
            CHECK(hasError(eqs::parseQuerySpec(document), "generator.params.speed", "unknown parameter"));
        }
        SUBCASE("missing and empty required params")
        {
            json document = nearestEnemy();
            document["generator"]["params"].erase("name");
            CHECK(hasError(eqs::parseQuerySpec(document), "generator.params.name", "missing required"));
            document["generator"]["params"]["name"] = "";
            CHECK(hasError(eqs::parseQuerySpec(document), "generator.params.name", "must not be empty"));
        }
        SUBCASE("wrong kinds and ranges")
        {
            json document = nearestEnemy();
            document["generator"]["params"]["radius"] = "far";
            CHECK(hasError(eqs::parseQuerySpec(document), "generator.params.radius", "must be a number"));
            document["generator"]["params"]["radius"] = 2000;
            CHECK(hasError(eqs::parseQuerySpec(document), "generator.params.radius", "must be between"));
            document["generator"]["params"]["radius"] = 25;
            document["generator"]["params"]["includeInactive"] = 1;
            CHECK(hasError(eqs::parseQuerySpec(document), "generator.params.includeInactive", "true or false"));
        }
        SUBCASE("Int params take whole numbers only (2.0 counts as 2)")
        {
            json document = nearestEnemy();
            document["generator"]["params"]["maxCount"] = 2.5;
            CHECK(hasError(eqs::parseQuerySpec(document), "generator.params.maxCount", "whole number"));
            document["generator"]["params"]["maxCount"] = 3.0;
            const auto accepted = eqs::parseQuerySpec(document);
            REQUIRE(accepted.spec.has_value());
            CHECK(std::get<int32_t>(accepted.spec->generator.params.at("maxCount")) == 3);
        }
        SUBCASE("unknown enum value")
        {
            json document = nearestEnemy();
            document["generator"]["params"]["match"] = "fuzzy";
            CHECK(hasError(eqs::parseQuerySpec(document), "generator.params.match", "unknown value 'fuzzy'"));
        }
        SUBCASE("a test needs exactly one of filter and score")
        {
            json document = nearestEnemy();
            document["tests"][0]["filter"] = json::object();
            CHECK(hasError(eqs::parseQuerySpec(document), "tests[0]", "both"));
            document["tests"][0].erase("filter");
            document["tests"][0].erase("score");
            CHECK(hasError(eqs::parseQuerySpec(document), "tests[0]", "needs 'filter' or 'score'"));
        }
        SUBCASE("filter and score blocks")
        {
            json document = nearestEnemy();
            document["tests"][0].erase("score");
            document["tests"][0]["filter"] = {{"min", 5}, {"max", 1}};
            CHECK(hasError(eqs::parseQuerySpec(document), "tests[0].filter", "above max"));
            document["tests"][0]["filter"] = {{"low", 1}};
            CHECK(hasError(eqs::parseQuerySpec(document), "tests[0].filter.low", "unknown key"));

            document["tests"][0].erase("filter");
            document["tests"][0]["score"] = {{"mode", "Sideways"}};
            CHECK(hasError(eqs::parseQuerySpec(document), "tests[0].score.mode"));
            document["tests"][0]["score"] = {{"weight", 11}};
            CHECK(hasError(eqs::parseQuerySpec(document), "tests[0].score.weight"));
            document["tests"][0]["score"] = {{"combine", "Max"}};
            CHECK(hasError(eqs::parseQuerySpec(document), "tests[0].score.combine"));
            document["tests"][0]["score"] = {{"speed", 1}};
            CHECK(hasError(eqs::parseQuerySpec(document), "tests[0].score.speed", "unknown key"));
        }
        SUBCASE("unknown test type and test params")
        {
            json document = nearestEnemy();
            document["tests"][0]["type"] = "Smell";
            CHECK(hasError(eqs::parseQuerySpec(document), "tests[0].type", "unknown test type 'Smell'"));
            document["tests"][0]["type"] = "Distance";
            document["tests"][0]["params"] = {{"to", "moon"}};
            CHECK(hasError(eqs::parseQuerySpec(document), "tests[0].params.to", "unknown value 'moon'"));
        }
        SUBCASE("tests must be an array; maxResults 1..64")
        {
            json document = nearestEnemy();
            document["tests"] = json::object();
            CHECK(hasError(eqs::parseQuerySpec(document), "tests", "must be an array"));

            document = nearestEnemy();
            document["maxResults"] = 0;
            CHECK(hasError(eqs::parseQuerySpec(document), "maxResults"));
            document["maxResults"] = 65;
            CHECK(hasError(eqs::parseQuerySpec(document), "maxResults"));
            document["maxResults"] = 64;
            CHECK(eqs::parseQuerySpec(document).errors.empty());
        }
        SUBCASE("every error is collected and no spec is returned")
        {
            json document = nearestEnemy();
            document["generator"]["params"]["radius"] = -1;
            document["maxResults"] = 0;
            document["extra"] = true;
            const auto result = eqs::parseQuerySpec(document);
            CHECK_FALSE(result.spec.has_value());
            CHECK(result.errors.size() >= 3);
            CHECK(hasError(result, "generator.params.radius"));
            CHECK(hasError(result, "maxResults"));
            CHECK(hasError(result, "extra"));
        }
    }

    TEST_CASE("caps: a generator's possible output, tighter with a PathCost test")
    {
        const json distanceScore = {{"type", "Distance"}, {"score", json::object()}};
        CHECK(eqs::parseQuerySpec(ringWithTest(distanceScore, 256, 4)).errors.empty()); // 1024
        const auto tooMany = eqs::parseQuerySpec(ringWithTest(distanceScore, 256, 5));    // 1280
        CHECK(hasError(tooMany, "generator.params", "1280"));

        const json pathFilter = {{"type", "PathCost"}, {"filter", json::object()}};
        CHECK(eqs::parseQuerySpec(ringWithTest(pathFilter, 128, 2)).errors.empty()); // 256
        const auto pathTooMany = eqs::parseQuerySpec(ringWithTest(pathFilter, 129, 2)); // 258
        CHECK(hasError(pathTooMany, "generator.params", "PathCost"));
    }

    TEST_CASE("filters: a boolean test's empty filter means must pass; bounds are kept as given")
    {
        auto filterOf = [](const json& test) -> eqs::EQSTestConfig
        {
            json document = json::object();
            document["generator"] = {{"type", "Grid"}};
            document["tests"] = json::array({test});
            const auto result = eqs::parseQuerySpec(document);
            REQUIRE(result.spec.has_value());
            return result.spec->tests[0].config;
        };

        const auto visible = filterOf({{"type", "Trace"}, {"filter", json::object()}});
        CHECK(visible.purpose == eqs::EQSTestPurpose::Filter);
        REQUIRE(visible.filterMin.has_value());
        CHECK(*visible.filterMin == doctest::Approx(1.0f));
        CHECK_FALSE(visible.filterMax.has_value());

        const auto hidden = filterOf({{"type", "Trace"}, {"filter", {{"max", 0}}}});
        CHECK_FALSE(hidden.filterMin.has_value());
        REQUIRE(hidden.filterMax.has_value());
        CHECK(*hidden.filterMax == doctest::Approx(0.0f));

        const auto reachable = filterOf({{"type", "PathCost"}, {"filter", json::object()}});
        CHECK_FALSE(reachable.filterMin.has_value());
        CHECK_FALSE(reachable.filterMax.has_value());

        const auto withinTen = filterOf({{"type", "Distance"}, {"filter", {{"max", 10}}}});
        REQUIRE(withinTen.filterMax.has_value());
        CHECK(*withinTen.filterMax == doctest::Approx(10.0f));
    }

    TEST_CASE("the canonical form round-trips, writes every param and its text is stable")
    {
        json document = nearestEnemy();
        document["tests"].push_back({{"type", "Trace"}, {"params", {{"from", "Target"}}}, {"filter", {{"max", 0}}}});
        document["tests"].push_back({{"type", "Dot"}, {"score", {{"weight", 0.1}, {"combine", "additive"}}}});
        const auto first = eqs::parseQuerySpec(document);
        REQUIRE(first.spec.has_value());

        const json canonical = eqs::querySpecToJson(*first.spec);
        CHECK(canonical["version"] == 1);
        CHECK(canonical["generator"]["params"].size() == 6); // every Entities param
        CHECK(canonical["tests"][0]["score"]["mode"] == "InverseLinear");
        CHECK(canonical["tests"][1]["params"]["from"] == "target");
        CHECK(canonical["tests"][1]["params"]["eyeHeight"].dump() == "1.6"); // shortest float text
        CHECK(canonical["tests"][2]["score"]["combine"] == "Additive");
        CHECK(canonical["tests"][2]["score"]["weight"].dump() == "0.1");

        const auto second = eqs::parseQuerySpec(canonical);
        REQUIRE(second.spec.has_value());
        CHECK(*second.spec == *first.spec);
        CHECK(eqs::querySpecToText(*second.spec) == eqs::querySpecToText(*first.spec));
        CHECK(eqs::querySpecToText(*first.spec) == canonical.dump(4));
    }

    TEST_CASE("parseQuerySpecText reports text that is not JSON")
    {
        CHECK(hasError(eqs::parseQuerySpecText("{ not json"), "", "not valid JSON"));
        CHECK(hasError(eqs::parseQuerySpecText(""), "", "not valid JSON"));
        CHECK(eqs::parseQuerySpecText(nearestEnemy().dump()).spec.has_value());
    }

    TEST_CASE("buildQuery makes the runnable query of a spec")
    {
        const auto parsed = eqs::parseQuerySpec(nearestEnemy());
        REQUIRE(parsed.spec.has_value());
        const eqs::EQSQueryDef def = eqs::buildQuery(*parsed.spec, "NearestEnemy");
        CHECK(def.name == "NearestEnemy");
        CHECK(def.maxResults == 1);
        REQUIRE(def.generator.get() != nullptr);
        CHECK(def.generator->getName() == "Entities");
        REQUIRE(def.tests.size() == 1);
        REQUIRE(def.tests[0].test.get() != nullptr);
        CHECK(def.tests[0].test->getName() == "Distance");
        CHECK(def.tests[0].config == parsed.spec->tests[0].config);
    }

    TEST_CASE("summarizeQuery and isValidQueryName")
    {
        const auto parsed = eqs::parseQuerySpec(nearestEnemy());
        REQUIRE(parsed.spec.has_value());
        const std::string summary = eqs::summarizeQuery(*parsed.spec);
        CHECK(summary.find("Entities(name Enemy") != std::string::npos);
        CHECK(summary.find("score Distance InverseLinear") != std::string::npos);
        CHECK(summary.find("1 result") != std::string::npos);

        CHECK(eqs::isValidQueryName("NearestEnemy"));
        CHECK(eqs::isValidQueryName("Cover_2"));
        CHECK(eqs::isValidQueryName(std::string(64, 'a')));
        CHECK_FALSE(eqs::isValidQueryName(std::string(65, 'a')));
        CHECK_FALSE(eqs::isValidQueryName(""));
        CHECK_FALSE(eqs::isValidQueryName("2Cover"));
        CHECK_FALSE(eqs::isValidQueryName("../secret"));
        CHECK_FALSE(eqs::isValidQueryName("a b"));
        CHECK_FALSE(eqs::isValidQueryName("_hidden"));
    }

    TEST_CASE("the shipped NearestEnemy template is valid and canonical")
    {
        namespace fs = std::filesystem;
        const auto root = repo_scan::findRepoRoot();
        REQUIRE_MESSAGE(root.has_value(), "could not locate the repo root from Tests.exe");
        const fs::path path = *root / "assets" / "eqs" / "NearestEnemy.vfEQSQuery";
        REQUIRE_MESSAGE(fs::exists(path), "missing " << path.string());

        std::ifstream file(path, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string text = buffer.str();

        const auto result = eqs::parseQuerySpecText(text);
        REQUIRE(result.errors.empty());
        REQUIRE(result.spec.has_value());
        CHECK(result.spec->generator.type == "Entities");
        CHECK(std::get<std::string>(result.spec->generator.params.at("name")) == "Enemy");
        CHECK(std::get<float>(result.spec->generator.params.at("radius")) == doctest::Approx(25.0f));
        REQUIRE(result.spec->tests.size() == 1);
        CHECK(result.spec->tests[0].type == "Distance");
        CHECK(result.spec->tests[0].config.mode == eqs::EQSScoringMode::InverseLinear);
        CHECK(result.spec->maxResults == 1);

        // The file is exactly the canonical text, apart from its CRLF line ends and final newline.
        std::string normalized;
        for (char c : text)
        {
            if (c != '\r')
                normalized += c;
        }
        while (!normalized.empty() && normalized.back() == '\n')
            normalized.pop_back();
        CHECK(normalized == eqs::querySpecToText(*result.spec));
    }
}
