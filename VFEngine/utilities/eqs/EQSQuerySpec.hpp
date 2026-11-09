#pragma once

#include "EQSQuery.hpp"
#include "EQSTypes.hpp"
#include <nlohmann/json_fwd.hpp>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// VK-1660: EQS query templates. One JSON file per query, assets/eqs/<Name>.vfEQSQuery; the file stem is the
// query's name:
//
//   {"version": 1, "description": "...",
//    "generator": {"type": "Ring|Grid|NavMeshPoints|Entities", "params": {...}},
//    "tests": [{"type": "Distance|Dot|OnNavMesh|PathCost|Trace", "params": {...},
//               "filter": {"min"?: n, "max"?: n}  |  "score": {"mode"?, "weight"?, "combine"?}}],
//    "maxResults": 1}
//
// Each test has exactly one of "filter" / "score". A boolean test (metric 1 or 0: OnNavMesh, Trace) whose
// filter gives no bound must pass (min 1). The catalogue below is the one description of every type, param,
// range and default (the parser, the MCP eqs_* tools and vf://docs/bt read it); parsing is strict and
// collects every error with its JSON path.
namespace eqs
{
    using EQSParamValue = std::variant<float, int32_t, bool, std::string>;

    enum class EQSParamKind : uint8_t
    {
        Float, // any JSON number, stored as float
        Int,   // integral numbers only
        Bool,
        Enum,  // a string from enumValues; case-insensitive on input, canonical on output
        String
    };

    struct EQSParamSpec
    {
        std::string name;
        EQSParamKind kind = EQSParamKind::Float;
        bool required = false;
        EQSParamValue defaultValue = 0.0f; // what an omitted optional param becomes
        bool hasRange = false;             // Float / Int: inclusive [minValue, maxValue]
        float minValue = 0.0f;
        float maxValue = 0.0f;
        std::vector<std::string> enumValues;
        std::string description;
    };

    struct EQSGeneratorTypeSpec
    {
        std::string type; // canonical
        std::vector<EQSParamSpec> params;
        std::string description;
    };

    struct EQSTestTypeSpec
    {
        std::string type; // canonical
        std::vector<EQSParamSpec> params;
        bool boolean = false; // the metric is 1 (pass) or 0
        std::string metric;   // what the metric measures, e.g. "metres to the reference point"
        std::string description;
    };

    const std::vector<EQSGeneratorTypeSpec>& generatorTypes(); // Ring, Grid, NavMeshPoints, Entities
    const std::vector<EQSTestTypeSpec>& testTypes();           // Distance, Dot, OnNavMesh, PathCost, Trace
    // Case-insensitive; nullptr when unknown.
    const EQSGeneratorTypeSpec* findGeneratorType(std::string_view type);
    const EQSTestTypeSpec* findTestType(std::string_view type);

    inline constexpr uint32_t kQuerySpecVersion = 1;
    // A generator's largest possible output (e.g. Ring pointCount x ringCount) must not exceed this ...
    inline constexpr uint32_t kMaxCandidates = 1024;
    // ... or this when the query has a PathCost test (one path search per candidate).
    inline constexpr uint32_t kMaxCandidatesWithPathCost = 256;
    inline constexpr uint32_t kMaxResultsLimit = 64; // maxResults 1..64
    inline constexpr float kMaxScoreWeight = 10.0f;  // score weight 0..10

    using EQSParamMap = std::map<std::string, EQSParamValue>; // ordered, so the canonical text is stable

    struct EQSGeneratorDecl
    {
        std::string type;   // canonical
        EQSParamMap params; // every catalogue param of the type (omitted ones at their default)

        bool operator==(const EQSGeneratorDecl&) const = default;
    };

    struct EQSTestDecl
    {
        std::string type;
        EQSParamMap params;
        EQSTestConfig config;

        bool operator==(const EQSTestDecl&) const = default;
    };

    struct EQSQuerySpec
    {
        std::string description;
        EQSGeneratorDecl generator;
        std::vector<EQSTestDecl> tests;
        uint32_t maxResults = 1;

        bool operator==(const EQSQuerySpec&) const = default;
    };

    struct EQSSpecError
    {
        std::string path; // JSON path, e.g. "tests[1].params.radius"; "" = the document itself
        std::string message;
    };

    struct EQSSpecParseResult
    {
        std::optional<EQSQuerySpec> spec; // set exactly when errors is empty
        std::vector<EQSSpecError> errors;
    };

    // Rejects: a non-object document, an unknown key, a version other than kQuerySpecVersion, an unknown
    // generator or test type or param, a missing required param, a value of the wrong kind or outside its
    // range, an unknown enum value, a test with both or neither of filter / score, a filter whose min is
    // above its max, unknown score keys, a generator output above the caps, maxResults outside 1..64.
    EQSSpecParseResult parseQuerySpec(const nlohmann::json& document);
    // Also reports text that is not JSON (path "").
    EQSSpecParseResult parseQuerySpecText(std::string_view text);

    // The canonical form: every key and param written, enums in canonical spelling, "version" =
    // kQuerySpecVersion. parseQuerySpec(querySpecToJson(spec)) reproduces `spec`.
    nlohmann::json querySpecToJson(const EQSQuerySpec& spec);
    std::string querySpecToText(const EQSQuerySpec& spec); // querySpecToJson(spec).dump(4)

    // The runnable query of a spec as parseQuerySpec returns it.
    EQSQueryDef buildQuery(const EQSQuerySpec& spec, const std::string& name);

    // One line for listings, e.g. "Entities(name Enemy, radius 25) | score Distance InverseLinear | 1 result".
    std::string summarizeQuery(const EQSQuerySpec& spec);

    // A template name: a letter, then up to 63 letters, digits or '_' (the file stem of the template).
    bool isValidQueryName(std::string_view name);
}
