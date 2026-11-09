#include "EQSToolSupport.hpp"
#include "SettingsTable.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <initializer_list>
#include <limits>
#include <map>
#include <numeric>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant>

namespace mcp::tools::eqstool
{
    using json = nlohmann::json;

    namespace
    {
        std::string_view paramKindName(eqs::EQSParamKind kind)
        {
            switch (kind)
            {
            case eqs::EQSParamKind::Float: return "float";
            case eqs::EQSParamKind::Int: return "int";
            case eqs::EQSParamKind::Bool: return "bool";
            case eqs::EQSParamKind::Enum: return "enum";
            case eqs::EQSParamKind::String: return "string";
            }
            return "float";
        }

        json paramListJson(const std::vector<eqs::EQSParamSpec>& params)
        {
            json list = json::array();
            for (const eqs::EQSParamSpec& param : params)
            {
                list.push_back(paramSpecJson(param));
            }
            return list;
        }

        // The key of the test at `index`; "test <index>" for an index the run has no test for.
        std::string testKey(const std::vector<RunTest>& tests, int index)
        {
            if (index >= 0 && static_cast<std::size_t>(index) < tests.size())
            {
                return tests[static_cast<std::size_t>(index)].key;
            }
            return std::format("test {}", index);
        }

        // Whether test `t` measured `candidate` (EQSQueryRunner): every test measures a kept candidate; the
        // filters run first, in declared order, so one dropped by filter f was measured by the filters up to f
        // and by nothing else. With the purposes unknown: whatever has a metric.
        bool measured(const std::vector<RunTest>& tests, const eqs::EQSDebugCandidate& candidate, std::size_t t,
                      float metric)
        {
            if (!tests[t].purpose.has_value())
            {
                return !std::isnan(metric);
            }
            if (candidate.filteredByTest < 0)
            {
                return true;
            }
            return *tests[t].purpose == eqs::EQSTestPurpose::Filter &&
                   t <= static_cast<std::size_t>(candidate.filteredByTest);
        }

        // A NaN total ranks below every number.
        float rankValue(float total)
        {
            return std::isnan(total) ? -std::numeric_limits<float>::infinity() : total;
        }
    }

    std::string_view scoringModeName(eqs::EQSScoringMode mode)
    {
        switch (mode)
        {
        case eqs::EQSScoringMode::Linear: return "Linear";
        case eqs::EQSScoringMode::InverseLinear: return "InverseLinear";
        case eqs::EQSScoringMode::Square: return "Square";
        case eqs::EQSScoringMode::Constant: return "Constant";
        }
        return "Linear";
    }

    std::string_view scoreCombineName(eqs::EQSScoreCombine combine)
    {
        switch (combine)
        {
        case eqs::EQSScoreCombine::Multiply: return "Multiply";
        case eqs::EQSScoreCombine::Additive: return "Additive";
        }
        return "Multiply";
    }

    std::string_view statusName(eqs::EQSQueryStatus status)
    {
        switch (status)
        {
        case eqs::EQSQueryStatus::Pending: return "Pending";
        case eqs::EQSQueryStatus::Running: return "Running";
        case eqs::EQSQueryStatus::Completed: return "Completed";
        case eqs::EQSQueryStatus::Failed: return "Failed";
        }
        return "Failed";
    }

    std::string hashToHex(uint64_t hash)
    {
        return std::format("{:016x}", hash);
    }

    std::optional<uint64_t> hexToHash(std::string_view text)
    {
        if (text.empty() || text.size() > 16)
        {
            return std::nullopt;
        }
        uint64_t value = 0;
        const char* end = text.data() + text.size();
        const std::from_chars_result parsed = std::from_chars(text.data(), end, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != end)
        {
            return std::nullopt;
        }
        return value;
    }

    std::string specErrorText(const eqs::EQSSpecError& error)
    {
        return error.path.empty() ? error.message : error.path + ": " + error.message;
    }

    json specErrorsJson(const std::vector<eqs::EQSSpecError>& errors)
    {
        json list = json::array();
        const std::size_t shown = std::min(errors.size(), maxReportedErrors);
        for (std::size_t i = 0; i < shown; ++i)
        {
            list.push_back({{"path", errors[i].path}, {"message", errors[i].message}});
        }
        return list;
    }

    std::string specErrorLines(const std::vector<eqs::EQSSpecError>& errors)
    {
        std::string text;
        const std::size_t shown = std::min(errors.size(), maxReportedErrors);
        for (std::size_t i = 0; i < shown; ++i)
        {
            if (!text.empty())
            {
                text += '\n';
            }
            text += "- " + specErrorText(errors[i]);
        }
        if (errors.size() > shown)
        {
            text += std::format("\n- ... and {} more", errors.size() - shown);
        }
        return text;
    }

    json paramValueJson(const eqs::EQSParamValue& value)
    {
        return std::visit([](const auto& v) -> json
        {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, float>)
            {
                return env::floatJson(v);
            }
            else
            {
                return json(v);
            }
        }, value);
    }

    json paramSpecJson(const eqs::EQSParamSpec& param)
    {
        json out = json::object();
        out["name"] = param.name;
        out["kind"] = std::string(paramKindName(param.kind));
        out["required"] = param.required;
        if (!param.required)
        {
            out["default"] = paramValueJson(param.defaultValue);
        }
        if (param.hasRange)
        {
            if (param.kind == eqs::EQSParamKind::Int)
            {
                // Stored as floats; the parser compares integers against the same casts.
                out["min"] = static_cast<int64_t>(param.minValue);
                out["max"] = static_cast<int64_t>(param.maxValue);
            }
            else
            {
                out["min"] = env::floatJson(param.minValue);
                out["max"] = env::floatJson(param.maxValue);
            }
        }
        if (param.kind == eqs::EQSParamKind::Enum)
        {
            out["values"] = param.enumValues;
        }
        out["description"] = param.description;
        return out;
    }

    json catalogJson()
    {
        json generators = json::array();
        for (const eqs::EQSGeneratorTypeSpec& type : eqs::generatorTypes())
        {
            json entry = json::object();
            entry["type"] = type.type;
            entry["description"] = type.description;
            entry["params"] = paramListJson(type.params);
            generators.push_back(std::move(entry));
        }

        json tests = json::array();
        for (const eqs::EQSTestTypeSpec& type : eqs::testTypes())
        {
            json entry = json::object();
            entry["type"] = type.type;
            entry["description"] = type.description;
            entry["metric"] = type.metric;
            entry["boolean"] = type.boolean;
            entry["params"] = paramListJson(type.params);
            tests.push_back(std::move(entry));
        }

        json modes = json::array();
        for (eqs::EQSScoringMode mode : {eqs::EQSScoringMode::Linear, eqs::EQSScoringMode::InverseLinear,
                                         eqs::EQSScoringMode::Square, eqs::EQSScoringMode::Constant})
        {
            modes.push_back(std::string(scoringModeName(mode)));
        }
        json combines = json::array();
        for (eqs::EQSScoreCombine combine : {eqs::EQSScoreCombine::Multiply, eqs::EQSScoreCombine::Additive})
        {
            combines.push_back(std::string(scoreCombineName(combine)));
        }
        const eqs::EQSTestConfig defaults{};

        json filter = json::object();
        filter["form"] = "{\"min\"?: number, \"max\"?: number}";
        filter["rule"] =
            "Keeps a candidate whose metric lies in [min, max]; a bound left out is open, and a candidate without "
            "a metric (PathCost: unreachable) is dropped. On a boolean test (OnNavMesh, Trace: metric 1 or 0) an "
            "empty filter {} means it must pass (min 1), {\"max\": 0} that it must fail.";

        json score = json::object();
        score["form"] = "{\"mode\"?, \"weight\"?, \"combine\"?}";
        score["modes"] = std::move(modes);
        score["defaultMode"] = std::string(scoringModeName(defaults.mode));
        score["combine"] = std::move(combines);
        score["defaultCombine"] = std::string(scoreCombineName(defaults.combine));
        score["weight"] = {
            {"min", 0},
            {"max", env::floatJson(eqs::kMaxScoreWeight)},
            {"default", env::floatJson(defaults.weight)}
        };
        score["rule"] =
            "The metric is min-max normalised over the survivors and shaped by mode: Linear = the highest metric "
            "is best, InverseLinear = the lowest is (Distance: nearest first), Square = Linear squared, Constant = "
            "1. A metric equal for every survivor shapes to 1 in every mode. The shaped value times weight is "
            "combined into the candidate's total, which starts at 1 (Multiply, or Additive). A candidate without a "
            "metric scores 0.";

        json limits = json::object();
        limits["maxCandidates"] = eqs::kMaxCandidates;
        limits["maxCandidatesWithPathCost"] = eqs::kMaxCandidatesWithPathCost;
        limits["maxResults"] = {{"min", 1}, {"max", eqs::kMaxResultsLimit}, {"default", eqs::EQSQuerySpec{}.maxResults}};
        limits["rule"] =
            "A generator's largest output (Ring pointCount x ringCount, Grid size x size, NavMeshPoints pointCount, "
            "Entities maxCount) must not exceed maxCandidates, or maxCandidatesWithPathCost when the query has a "
            "PathCost test (one path search per candidate).";

        json scoring = json::object();
        scoring["version"] = eqs::kQuerySpecVersion;
        scoring["order"] =
            "Filters run first, in declared order, each on the candidates the earlier filters kept; then the score "
            "tests, in declared order, on the survivors. The survivors are ranked by total (ties keep generation "
            "order) and the first maxResults are the result.";
        scoring["tests"] =
            "Each test has exactly one of filter / score. A param left out takes its default; a required one "
            "(Entities name) must be given.";
        scoring["context"] =
            "center / to / from name the querier (the entity running the query) or the query's target (a "
            "behavior-tree node's targetKey, a script's submitQueryWithTarget, eqs_run's target); a template that "
            "uses the target fails when it runs without one.";
        scoring["filter"] = std::move(filter);
        scoring["score"] = std::move(score);
        scoring["limits"] = std::move(limits);

        json catalog = json::object();
        catalog["generators"] = std::move(generators);
        catalog["tests"] = std::move(tests);
        catalog["scoring"] = std::move(scoring);
        return catalog;
    }

    json listEntryJson(const std::string& name, const std::string& path, const std::optional<std::string>& text,
                       const std::string& readError)
    {
        json entry = json::object();
        entry["name"] = name;
        entry["path"] = path;
        if (!text.has_value())
        {
            entry["valid"] = false;
            entry["error"] = readError;
            entry["errorCount"] = 1;
            entry["description"] = "";
            return entry;
        }

        const eqs::EQSSpecParseResult parsed = eqs::parseQuerySpecText(*text);
        if (parsed.spec.has_value())
        {
            json canonical = eqs::querySpecToJson(*parsed.spec);
            entry["valid"] = true;
            entry["description"] = std::move(canonical["description"]);
            entry["generator"] = std::move(canonical["generator"]);
            entry["tests"] = std::move(canonical["tests"]);
            entry["maxResults"] = std::move(canonical["maxResults"]);
            entry["summary"] = eqs::summarizeQuery(*parsed.spec);
            return entry;
        }

        const std::size_t count = parsed.errors.size();
        std::string error = count == 0 ? std::string("the template is invalid") : specErrorText(parsed.errors.front());
        if (count > 1)
        {
            error += std::format(" (and {} more; eqs_get_query lists them)", count - 1);
        }
        entry["valid"] = false;
        entry["error"] = std::move(error);
        entry["errorCount"] = std::max<std::size_t>(count, 1);

        // A template that is still a JSON object names itself, even when its checks fail.
        std::string description;
        const json document = json::parse(*text, nullptr, false);
        if (document.is_object())
        {
            const auto it = document.find("description");
            if (it != document.end() && it->is_string())
            {
                description = it->get<std::string>();
            }
        }
        entry["description"] = std::move(description);
        return entry;
    }

    std::vector<RunTest> runTests(const std::vector<std::string>& testNames, const eqs::EQSQuerySpec* spec)
    {
        bool matches = spec != nullptr && spec->tests.size() == testNames.size();
        for (std::size_t i = 0; matches && i < testNames.size(); ++i)
        {
            matches = spec->tests[i].type == testNames[i];
        }

        std::map<std::string, int> totals;
        for (const std::string& name : testNames)
        {
            ++totals[name];
        }

        std::vector<RunTest> tests;
        tests.reserve(testNames.size());
        std::map<std::string, int> seen;
        for (std::size_t i = 0; i < testNames.size(); ++i)
        {
            const std::string& name = testNames[i];
            RunTest test;
            test.type = name;
            test.key = totals[name] > 1 ? std::format("{}#{}", name, ++seen[name]) : name;
            if (matches)
            {
                test.purpose = spec->tests[i].config.purpose;
            }
            tests.push_back(std::move(test));
        }
        return tests;
    }

    json runReplyJson(const eqs::EQSDebugResult& run, const std::vector<RunTest>& tests, std::size_t maxCandidates,
                      const std::function<json(uint64_t entityId)>& entityJson)
    {
        std::map<uint64_t, json> entities;
        auto entityOf = [&entities, &entityJson](uint64_t entityId) -> const json&
        {
            auto it = entities.find(entityId);
            if (it == entities.end())
            {
                json entity = entityJson ? entityJson(entityId) : json::object({{"id", entityId}});
                it = entities.emplace(entityId, std::move(entity)).first;
            }
            return it->second;
        };

        const eqs::EQSResult& result = run.result;
        const bool completed = result.status == eqs::EQSQueryStatus::Completed;

        json out = json::object();
        out["status"] = std::string(statusName(result.status));
        if (!result.error.empty())
        {
            out["error"] = result.error;
        }

        if (completed && !result.candidates.empty())
        {
            const eqs::EQSCandidate& best = result.candidates.front();
            json entry = json::object();
            entry["position"] = env::vec3Json(best.position);
            if (best.entityId != eqs::kNoEntity)
            {
                entry["entity"] = entityOf(best.entityId);
            }
            entry["score"] = env::floatJson(best.totalScore);
            out["best"] = std::move(entry);
        }
        else
        {
            out["best"] = nullptr;
        }

        std::size_t kept = 0;
        json filteredBy = json::object();
        if (completed)
        {
            for (const eqs::EQSDebugCandidate& candidate : run.candidates)
            {
                if (candidate.filteredByTest < 0)
                {
                    ++kept;
                    continue;
                }
                const std::string key = testKey(tests, candidate.filteredByTest);
                filteredBy[key] = filteredBy.value(key, 0) + 1;
            }
        }
        json counts = json::object();
        counts["generated"] = run.candidates.size();
        counts["filteredBy"] = std::move(filteredBy);
        counts["scored"] = kept;
        counts["results"] = completed ? result.candidates.size() : std::size_t{0};
        out["counts"] = std::move(counts);

        bool purposesKnown = true;
        json testList = json::array();
        for (const RunTest& test : tests)
        {
            json purpose = nullptr;
            if (test.purpose.has_value())
            {
                purpose = *test.purpose == eqs::EQSTestPurpose::Filter ? "filter" : "score";
            }
            else
            {
                purposesKnown = false;
            }
            json entry = json::object();
            entry["key"] = test.key;
            entry["type"] = test.type;
            entry["purpose"] = std::move(purpose);
            testList.push_back(std::move(entry));
        }
        out["tests"] = std::move(testList);

        std::vector<std::string> notes;
        json candidateList = json::array();
        if (completed)
        {
            // The kept candidates by total, ties in generation order (the runner's stable ranking, so the first
            // one is the best); then the dropped ones in generation order.
            std::vector<std::size_t> order(run.candidates.size());
            std::iota(order.begin(), order.end(), std::size_t{0});
            std::stable_sort(order.begin(), order.end(), [&run](std::size_t a, std::size_t b)
            {
                const eqs::EQSDebugCandidate& first = run.candidates[a];
                const eqs::EQSDebugCandidate& second = run.candidates[b];
                const bool firstKept = first.filteredByTest < 0;
                const bool secondKept = second.filteredByTest < 0;
                if (firstKept != secondKept)
                {
                    return firstKept;
                }
                return firstKept && rankValue(first.total) > rankValue(second.total);
            });

            const std::size_t shown = std::min(order.size(), maxCandidates);
            for (std::size_t rank = 0; rank < shown; ++rank)
            {
                const eqs::EQSDebugCandidate& candidate = run.candidates[order[rank]];
                const bool isKept = candidate.filteredByTest < 0;

                json entry = json::object();
                entry["position"] = env::vec3Json(candidate.position);
                if (candidate.entityId != eqs::kNoEntity)
                {
                    entry["entity"] = entityOf(candidate.entityId);
                }
                entry["total"] = env::floatJson(candidate.total);
                if (!isKept)
                {
                    entry["filteredBy"] = testKey(tests, candidate.filteredByTest);
                }

                json metrics = json::object();
                json scores = json::object();
                for (std::size_t t = 0; t < tests.size(); ++t)
                {
                    const float metric = t < candidate.metrics.size() ? candidate.metrics[t] : eqs::kNoMetric;
                    if (measured(tests, candidate, t, metric))
                    {
                        metrics[tests[t].key] = env::floatJson(metric); // NaN (no metric) -> null
                    }
                    const bool scoreTest = !tests[t].purpose.has_value() ||
                                           *tests[t].purpose == eqs::EQSTestPurpose::Score;
                    if (isKept && scoreTest && t < candidate.scores.size())
                    {
                        scores[tests[t].key] = env::floatJson(candidate.scores[t]);
                    }
                }
                entry["metrics"] = std::move(metrics);
                entry["scores"] = std::move(scores);
                candidateList.push_back(std::move(entry));
            }

            if (run.candidates.empty())
            {
                notes.push_back("The generator made no candidates (Entities: no entity of that name within the "
                                "radius; NavMeshPoints: no navmesh near the samples).");
            }
            else if (kept == 0)
            {
                notes.push_back("No candidate passed the filters: counts.filteredBy says which filter dropped them.");
            }
            if (order.size() > shown)
            {
                notes.push_back(std::format("candidates lists {} of the {} candidates (the kept ones first); raise "
                                            "maxCandidates (up to {}) to see more.",
                                            shown, order.size(), maxCandidatesLimit));
            }
        }
        if (!purposesKnown)
        {
            notes.push_back("The template file does not match the copy the engine ran (changed outside eqs_set_query "
                            "since the engine cached it?), so filters and scores cannot be told apart: scores lists "
                            "every test. Saving it with eqs_set_query reloads it.");
        }

        out["candidates"] = std::move(candidateList);
        out["timeMs"] = std::round(run.elapsedMs * 1000.0) / 1000.0;
        out["notes"] = std::move(notes);
        return out;
    }
}
