#pragma once

#include "eqs/EQSQuerySpec.hpp"
#include "eqs/EQSTypes.hpp"

#include <nlohmann/json.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// VK-1660 helpers of the eqs_* tools (EQSTools.cpp): the catalogue and template JSON, content hashes, the keys a
// run's tests are reported under and the eqs_run reply. A template is assets/eqs/<name>.vfEQSQuery
// (utilities/eqs/EQSQuerySpec.hpp); the tools name templates and never take a path.
//
// Every function here is pure: any thread, nothing is dispatched (the tests drive them directly).
namespace mcp::tools::eqstool
{
    // eqs_list_queries is a Worker tool, so it keeps its own deadline (the server enforces none).
    inline constexpr std::chrono::milliseconds listDeadline{10000};
    // eqs_set_query: check, guard, write, and the reload its AssetSavedNotification triggers.
    inline constexpr std::chrono::milliseconds setTimeout{15000};
    // Spec errors listed in one reply; the others are counted.
    inline constexpr std::size_t maxReportedErrors = 20;
    // The largest template file a listing reads (EQSAuthoringService refuses larger ones too).
    inline constexpr std::uintmax_t maxTemplateBytes = 1024 * 1024;

    // eqs_run arguments.
    inline constexpr int64_t maxCandidatesLimit = 200;
    inline constexpr int64_t defaultMaxCandidates = 50;
    inline constexpr double minShowSeconds = 1.0;
    inline constexpr double maxShowSeconds = 60.0;
    inline constexpr double defaultShowSeconds = 10.0;

    // What eqs::isValidQueryName accepts, as the JSON-Schema pattern of the tools' name argument.
    inline constexpr std::string_view queryNamePattern = "^[A-Za-z][A-Za-z0-9_]{0,63}$";

    // The canonical spelling a template's score uses (EQSQuerySpec.cpp; the tests parse every one back).
    std::string_view scoringModeName(eqs::EQSScoringMode mode);
    std::string_view scoreCombineName(eqs::EQSScoreCombine combine);

    // "Pending", "Running", "Completed" or "Failed".
    std::string_view statusName(eqs::EQSQueryStatus status);

    // 16 lowercase hex digits: how every contentHash is reported (the client is JavaScript, whose numbers cannot
    // hold every 64-bit value).
    std::string hashToHex(uint64_t hash);
    // 1 to 16 hex digits in either case; nullopt for anything else (no sign, prefix or spaces).
    std::optional<uint64_t> hexToHash(std::string_view text);

    // "path: message", or the message alone for an error of the whole document (path "").
    std::string specErrorText(const eqs::EQSSpecError& error);
    // [{path, message}] of the first maxReportedErrors errors.
    nlohmann::json specErrorsJson(const std::vector<eqs::EQSSpecError>& errors);
    // The first maxReportedErrors errors, one "- ..." line each, then "- ... and N more" when there are more.
    std::string specErrorLines(const std::vector<eqs::EQSSpecError>& errors);

    // A param value in its template JSON form (a float as its shortest round trip).
    nlohmann::json paramValueJson(const eqs::EQSParamValue& value);
    // One catalogue param: {name, kind, required, default (optional params), min?, max?, values?, description}.
    nlohmann::json paramSpecJson(const eqs::EQSParamSpec& param);
    // The catalogue: {generators, tests, scoring} - eqs::generatorTypes() and eqs::testTypes() with their
    // params, the filter and score rules, and the limits a template must keep.
    nlohmann::json catalogJson();

    // One eqs_list_queries entry for the template `name`, reported at `path`. From `text` (the file's text):
    // {name, path, valid:true, description, generator, tests, maxResults, summary} in canonical form, or
    // {name, path, valid:false, error, errorCount, description} with the first error. `text` nullopt = the file
    // could not be read; `readError` says why.
    nlohmann::json listEntryJson(const std::string& name, const std::string& path,
                                 const std::optional<std::string>& text, const std::string& readError);

    // One test of a run, as eqs_run reports it.
    struct RunTest
    {
        // Its key in counts.filteredBy, metrics and scores: the type, or "<type>#<n>" (n from 1, in declared
        // order) when the query has that type more than once.
        std::string key;
        std::string type; // EQSDebugResult::testNames
        // Filter / Score, from the template file; nullopt when that file does not describe the run (it was
        // changed after the engine cached the template).
        std::optional<eqs::EQSTestPurpose> purpose;
    };

    // The run's tests. Purposes are read from `spec` when its tests have the run's types in the same order.
    std::vector<RunTest> runTests(const std::vector<std::string>& testNames, const eqs::EQSQuerySpec* spec);

    // eqs_run's report of a run (the tool adds name, context and its own notes):
    //   {status, error?, best: {position, entity?, score} | null,
    //    counts: {generated, filteredBy: {key: n}, scored, results},
    //    tests: [{key, type, purpose: "filter" | "score" | null}],
    //    candidates: [{position, entity?, total, filteredBy?, metrics: {key: n | null}, scores: {key: n}}],
    //    timeMs, notes}
    // Candidates: the kept ones by total (ties in generation order, as the runner ranks them), then the dropped
    // ones in generation order, at most `maxCandidates`; none for a failed run. `metrics` holds every test that
    // measured the candidate (null = it has no metric, e.g. PathCost unreachable), `scores` the score tests of
    // a kept candidate. `entityJson` gives an entity's {id, name}; it is called once per entity id.
    nlohmann::json runReplyJson(const eqs::EQSDebugResult& run, const std::vector<RunTest>& tests,
                                std::size_t maxCandidates,
                                const std::function<nlohmann::json(uint64_t entityId)>& entityJson);
}
