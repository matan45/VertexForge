#include "EQSQuerySpec.hpp"
#include "generators/EntitiesGenerator.hpp"
#include "generators/GridGenerator.hpp"
#include "generators/NavMeshPointsGenerator.hpp"
#include "generators/RingGenerator.hpp"
#include "tests/DistanceTest.hpp"
#include "tests/DotProductTest.hpp"
#include "tests/OnNavMeshTest.hpp"
#include "tests/PathfindingCostTest.hpp"
#include "tests/TraceTest.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <initializer_list>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
#include <variant>

namespace eqs
{
    using json = nlohmann::json;

    namespace
    {
        // ---- Catalogue construction ----

        EQSParamSpec floatParam(std::string name, float seed, float minValue, float maxValue, std::string description)
        {
            EQSParamSpec param;
            param.name = std::move(name);
            param.kind = EQSParamKind::Float;
            param.defaultValue = seed;
            param.hasRange = true;
            param.minValue = minValue;
            param.maxValue = maxValue;
            param.description = std::move(description);
            return param;
        }

        EQSParamSpec intParam(std::string name, int32_t seed, int32_t minValue, int32_t maxValue,
                              std::string description)
        {
            EQSParamSpec param;
            param.name = std::move(name);
            param.kind = EQSParamKind::Int;
            param.defaultValue = seed;
            param.hasRange = true;
            param.minValue = static_cast<float>(minValue);
            param.maxValue = static_cast<float>(maxValue);
            param.description = std::move(description);
            return param;
        }

        EQSParamSpec boolParam(std::string name, bool seed, std::string description)
        {
            EQSParamSpec param;
            param.name = std::move(name);
            param.kind = EQSParamKind::Bool;
            param.defaultValue = seed;
            param.description = std::move(description);
            return param;
        }

        EQSParamSpec enumParam(std::string name, std::vector<std::string> values, std::string seed,
                               std::string description)
        {
            EQSParamSpec param;
            param.name = std::move(name);
            param.kind = EQSParamKind::Enum;
            param.defaultValue = std::move(seed);
            param.enumValues = std::move(values);
            param.description = std::move(description);
            return param;
        }

        EQSParamSpec stringParam(std::string name, bool required, std::string description)
        {
            EQSParamSpec param;
            param.name = std::move(name);
            param.kind = EQSParamKind::String;
            param.required = required;
            param.defaultValue = std::string();
            param.description = std::move(description);
            return param;
        }

        // "center" / "to" / "from": the querier or the query's target.
        EQSParamSpec contextPointParam(std::string name, std::string description)
        {
            return enumParam(std::move(name), {"querier", "target"}, "querier", std::move(description));
        }

        constexpr std::array<std::pair<EQSScoringMode, std::string_view>, 4> kScoringModes{{
            {EQSScoringMode::Linear, "Linear"},
            {EQSScoringMode::InverseLinear, "InverseLinear"},
            {EQSScoringMode::Square, "Square"},
            {EQSScoringMode::Constant, "Constant"},
        }};

        constexpr std::array<std::pair<EQSScoreCombine, std::string_view>, 2> kScoreCombines{{
            {EQSScoreCombine::Multiply, "Multiply"},
            {EQSScoreCombine::Additive, "Additive"},
        }};

        std::string_view scoringModeName(EQSScoringMode mode)
        {
            for (const auto& [value, name] : kScoringModes)
            {
                if (value == mode)
                    return name;
            }
            return "Linear";
        }

        std::string_view scoreCombineName(EQSScoreCombine combine)
        {
            for (const auto& [value, name] : kScoreCombines)
            {
                if (value == combine)
                    return name;
            }
            return "Multiply";
        }

        // ---- Text helpers ----

        char lowerAscii(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool equalsIgnoreCase(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (size_t i = 0; i < a.size(); ++i)
            {
                if (lowerAscii(a[i]) != lowerAscii(b[i]))
                    return false;
            }
            return true;
        }

        std::string joinPath(const std::string& base, std::string_view key)
        {
            return base.empty() ? std::string(key) : base + "." + std::string(key);
        }

        template <typename Range>
        std::string joinNames(const Range& names)
        {
            std::string text;
            for (const auto& name : names)
            {
                if (!text.empty())
                    text += ", ";
                text += name;
            }
            return text;
        }

        std::string paramNames(const std::vector<EQSParamSpec>& params)
        {
            std::vector<std::string> names;
            names.reserve(params.size());
            for (const EQSParamSpec& param : params)
                names.push_back(param.name);
            return joinNames(names);
        }

        void addError(std::vector<EQSSpecError>& errors, std::string path, std::string message)
        {
            errors.push_back(EQSSpecError{std::move(path), std::move(message)});
        }

        // A JSON integer, or a float with no fractional part (2.0 is accepted as 2, 2.5 is not).
        std::optional<int64_t> integralValue(const json& value)
        {
            if (value.is_number_unsigned())
            {
                const uint64_t number = value.get<uint64_t>();
                if (number > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
                    return std::nullopt;
                return static_cast<int64_t>(number);
            }
            if (value.is_number_integer())
                return value.get<int64_t>();
            if (value.is_number_float())
            {
                const double number = value.get<double>();
                if (!std::isfinite(number) || std::floor(number) != number || std::abs(number) > 9.0e15)
                    return std::nullopt;
                return static_cast<int64_t>(number);
            }
            return std::nullopt;
        }

        // A float as the JSON number of its shortest round-trip decimal text, so 0.1f is written "0.1", not
        // "0.10000000149011612"; parsing that back as a float gives the same float.
        json floatToJson(float value)
        {
            return json(std::stod(std::format("{}", value)));
        }

        // ---- Strict parsing ----

        void checkKeys(const json& object, std::initializer_list<std::string_view> allowed, const std::string& path,
                       std::vector<EQSSpecError>& errors)
        {
            for (auto it = object.begin(); it != object.end(); ++it)
            {
                const std::string& key = it.key();
                if (std::find(allowed.begin(), allowed.end(), std::string_view(key)) == allowed.end())
                    addError(errors, joinPath(path, key), std::format("unknown key '{}' (expected {})", key, joinNames(allowed)));
            }
        }

        std::optional<EQSParamValue> parseParamValue(const EQSParamSpec& spec, const json& value,
                                                     const std::string& path, std::vector<EQSSpecError>& errors)
        {
            switch (spec.kind)
            {
            case EQSParamKind::Float:
            {
                if (!value.is_number())
                {
                    addError(errors, path, std::format("must be a number (got {})", value.type_name()));
                    return std::nullopt;
                }
                const double number = value.get<double>();
                const float parsed = static_cast<float>(number);
                if (!std::isfinite(number) || !std::isfinite(parsed))
                {
                    addError(errors, path, "must be a finite number");
                    return std::nullopt;
                }
                if (spec.hasRange && (parsed < spec.minValue || parsed > spec.maxValue))
                {
                    addError(errors, path, std::format("must be between {} and {} (got {})",
                                                       spec.minValue, spec.maxValue, parsed));
                    return std::nullopt;
                }
                return EQSParamValue{parsed};
            }
            case EQSParamKind::Int:
            {
                const std::optional<int64_t> number = integralValue(value);
                if (!number)
                {
                    addError(errors, path, std::format("must be a whole number (got {})", value.dump()));
                    return std::nullopt;
                }
                const auto low = static_cast<int64_t>(spec.minValue);
                const auto high = static_cast<int64_t>(spec.maxValue);
                if (spec.hasRange && (*number < low || *number > high))
                {
                    addError(errors, path, std::format("must be between {} and {} (got {})", low, high, *number));
                    return std::nullopt;
                }
                return EQSParamValue{static_cast<int32_t>(*number)};
            }
            case EQSParamKind::Bool:
            {
                if (!value.is_boolean())
                {
                    addError(errors, path, std::format("must be true or false (got {})", value.type_name()));
                    return std::nullopt;
                }
                return EQSParamValue{value.get<bool>()};
            }
            case EQSParamKind::Enum:
            {
                if (value.is_string())
                {
                    const std::string text = value.get<std::string>();
                    for (const std::string& option : spec.enumValues)
                    {
                        if (equalsIgnoreCase(option, text))
                            return EQSParamValue{option};
                    }
                    addError(errors, path, std::format("unknown value '{}' (expected {})", text, joinNames(spec.enumValues)));
                    return std::nullopt;
                }
                addError(errors, path, std::format("must be one of {} (got {})", joinNames(spec.enumValues), value.type_name()));
                return std::nullopt;
            }
            case EQSParamKind::String:
            {
                if (!value.is_string())
                {
                    addError(errors, path, std::format("must be a string (got {})", value.type_name()));
                    return std::nullopt;
                }
                std::string text = value.get<std::string>();
                if (spec.required && text.empty())
                {
                    addError(errors, path, "must not be empty");
                    return std::nullopt;
                }
                return EQSParamValue{std::move(text)};
            }
            }
            return std::nullopt;
        }

        // Every catalogue param of the type ends up in `out` (an omitted or invalid one at its default, so later
        // checks still see a complete map); unknown, missing, wrongly typed and out-of-range values are errors.
        void parseParams(const std::vector<EQSParamSpec>& specs, const std::string& typeName, const json* params,
                         const std::string& path, EQSParamMap& out, std::vector<EQSSpecError>& errors)
        {
            if (params && !params->is_object())
            {
                addError(errors, path, std::format("must be an object (got {})", params->type_name()));
                params = nullptr;
            }
            if (params)
            {
                for (auto it = params->begin(); it != params->end(); ++it)
                {
                    const std::string& key = it.key();
                    const bool known = std::any_of(specs.begin(), specs.end(),
                                                   [&key](const EQSParamSpec& spec) { return spec.name == key; });
                    if (!known)
                        addError(errors, joinPath(path, key), std::format("unknown parameter '{}' ({} takes {})", key, typeName, paramNames(specs)));
                }
            }

            for (const EQSParamSpec& spec : specs)
            {
                const std::string paramPath = joinPath(path, spec.name);
                const json* value = nullptr;
                if (params)
                {
                    auto it = params->find(spec.name);
                    if (it != params->end())
                        value = &*it;
                }
                if (!value)
                {
                    if (spec.required)
                        addError(errors, paramPath, std::format("missing required parameter '{}'", spec.name));
                    out[spec.name] = spec.defaultValue;
                    continue;
                }
                std::optional<EQSParamValue> parsed = parseParamValue(spec, *value, paramPath, errors);
                out[spec.name] = parsed ? std::move(*parsed) : spec.defaultValue;
            }
        }

        std::optional<float> parseBound(const json& filter, const char* key, const std::string& path,
                                        std::vector<EQSSpecError>& errors)
        {
            auto it = filter.find(key);
            if (it == filter.end())
                return std::nullopt;
            if (!it->is_number())
            {
                addError(errors, joinPath(path, key), std::format("must be a number (got {})", it->type_name()));
                return std::nullopt;
            }
            const float bound = static_cast<float>(it->get<double>());
            if (!std::isfinite(bound))
            {
                addError(errors, joinPath(path, key), "must be a finite number");
                return std::nullopt;
            }
            return bound;
        }

        void parseFilter(const json& filter, const std::string& path, bool booleanTest, EQSTestConfig& config,
                         std::vector<EQSSpecError>& errors)
        {
            config.purpose = EQSTestPurpose::Filter;
            if (!filter.is_object())
            {
                addError(errors, path, std::format("must be an object {{\"min\"?, \"max\"?}} (got {})", filter.type_name()));
                return;
            }
            checkKeys(filter, {"min", "max"}, path, errors);
            config.filterMin = parseBound(filter, "min", path, errors);
            config.filterMax = parseBound(filter, "max", path, errors);
            if (config.filterMin && config.filterMax && *config.filterMin > *config.filterMax)
                addError(errors, path, std::format("min ({}) is above max ({})", *config.filterMin, *config.filterMax));
            // A boolean test (metric 1 = pass) without a bound means "must pass".
            if (booleanTest && !config.filterMin && !config.filterMax)
                config.filterMin = 1.0f;
        }

        void parseScore(const json& score, const std::string& path, EQSTestConfig& config,
                        std::vector<EQSSpecError>& errors)
        {
            config.purpose = EQSTestPurpose::Score;
            if (!score.is_object())
            {
                addError(errors, path, std::format("must be an object {{\"mode\"?, \"weight\"?, \"combine\"?}} (got {})", score.type_name()));
                return;
            }
            checkKeys(score, {"mode", "weight", "combine"}, path, errors);

            if (auto it = score.find("mode"); it != score.end())
            {
                bool found = false;
                if (it->is_string())
                {
                    const std::string text = it->get<std::string>();
                    for (const auto& [value, name] : kScoringModes)
                    {
                        if (equalsIgnoreCase(name, text))
                        {
                            config.mode = value;
                            found = true;
                        }
                    }
                }
                if (!found)
                    addError(errors, joinPath(path, "mode"), std::format("must be one of Linear, InverseLinear, Square, Constant (got {})", it->dump()));
            }

            if (auto it = score.find("weight"); it != score.end())
            {
                const double weight = it->is_number() ? it->get<double>() : std::numeric_limits<double>::quiet_NaN();
                if (!(weight >= 0.0 && weight <= static_cast<double>(kMaxScoreWeight)))
                    addError(errors, joinPath(path, "weight"), std::format("must be a number from 0 to {} (got {})", kMaxScoreWeight, it->dump()));
                else
                    config.weight = static_cast<float>(weight);
            }

            if (auto it = score.find("combine"); it != score.end())
            {
                bool found = false;
                if (it->is_string())
                {
                    const std::string text = it->get<std::string>();
                    for (const auto& [value, name] : kScoreCombines)
                    {
                        if (equalsIgnoreCase(name, text))
                        {
                            config.combine = value;
                            found = true;
                        }
                    }
                }
                if (!found)
                    addError(errors, joinPath(path, "combine"), std::format("must be Multiply or Additive (got {})", it->dump()));
            }
        }

        // ---- Param access (a parsed spec has every param) ----

        float floatOf(const EQSParamMap& params, const char* name, float fallback)
        {
            auto it = params.find(name);
            if (it != params.end())
            {
                if (const float* value = std::get_if<float>(&it->second))
                    return *value;
            }
            return fallback;
        }

        int32_t intOf(const EQSParamMap& params, const char* name, int32_t fallback)
        {
            auto it = params.find(name);
            if (it != params.end())
            {
                if (const int32_t* value = std::get_if<int32_t>(&it->second))
                    return *value;
            }
            return fallback;
        }

        bool boolOf(const EQSParamMap& params, const char* name, bool fallback)
        {
            auto it = params.find(name);
            if (it != params.end())
            {
                if (const bool* value = std::get_if<bool>(&it->second))
                    return *value;
            }
            return fallback;
        }

        std::string stringOf(const EQSParamMap& params, const char* name, const std::string& fallback)
        {
            auto it = params.find(name);
            if (it != params.end())
            {
                if (const std::string* value = std::get_if<std::string>(&it->second))
                    return *value;
            }
            return fallback;
        }

        EQSContextPoint contextPointOf(const EQSParamMap& params, const char* name)
        {
            return stringOf(params, name, "querier") == "target" ? EQSContextPoint::Target : EQSContextPoint::Querier;
        }

        // The most candidates a (valid) generator declaration can produce.
        uint64_t maxGeneratorOutput(const EQSGeneratorDecl& generator)
        {
            auto count = [&generator](const char* name) -> uint64_t
            {
                const int32_t value = intOf(generator.params, name, 0);
                return value > 0 ? static_cast<uint64_t>(value) : 0;
            };
            if (generator.type == "Ring")
                return count("pointCount") * count("ringCount");
            if (generator.type == "Grid")
                return count("size") * count("size");
            if (generator.type == "NavMeshPoints")
                return count("pointCount");
            if (generator.type == "Entities")
                return count("maxCount");
            return 0;
        }

        std::shared_ptr<const IEQSGenerator> makeGenerator(const EQSGeneratorDecl& generator)
        {
            const EQSParamMap& p = generator.params;
            if (generator.type == "Ring")
            {
                RingGenerator::Settings settings;
                settings.center = contextPointOf(p, "center");
                settings.radius = floatOf(p, "radius", settings.radius);
                settings.pointCount = intOf(p, "pointCount", settings.pointCount);
                settings.ringCount = intOf(p, "ringCount", settings.ringCount);
                settings.ringSpacing = floatOf(p, "ringSpacing", settings.ringSpacing);
                return std::make_shared<RingGenerator>(settings);
            }
            if (generator.type == "Grid")
            {
                GridGenerator::Settings settings;
                settings.center = contextPointOf(p, "center");
                settings.size = intOf(p, "size", settings.size);
                settings.spacing = floatOf(p, "spacing", settings.spacing);
                return std::make_shared<GridGenerator>(settings);
            }
            if (generator.type == "NavMeshPoints")
            {
                NavMeshPointsGenerator::Settings settings;
                settings.center = contextPointOf(p, "center");
                settings.radius = floatOf(p, "radius", settings.radius);
                settings.pointCount = intOf(p, "pointCount", settings.pointCount);
                settings.snapRadius = floatOf(p, "snapRadius", settings.snapRadius);
                return std::make_shared<NavMeshPointsGenerator>(settings);
            }
            if (generator.type == "Entities")
            {
                EntitiesGenerator::Settings settings;
                settings.center = contextPointOf(p, "center");
                settings.radius = floatOf(p, "radius", settings.radius);
                settings.name = stringOf(p, "name", settings.name);
                settings.prefix = stringOf(p, "match", "exact") == "prefix";
                settings.includeInactive = boolOf(p, "includeInactive", settings.includeInactive);
                settings.maxCount = intOf(p, "maxCount", settings.maxCount);
                return std::make_shared<EntitiesGenerator>(std::move(settings));
            }
            return nullptr;
        }

        std::shared_ptr<const IEQSTest> makeTest(const EQSTestDecl& test)
        {
            const EQSParamMap& p = test.params;
            if (test.type == "Distance")
            {
                DistanceTest::Settings settings;
                settings.to = contextPointOf(p, "to");
                settings.horizontal = boolOf(p, "horizontal", settings.horizontal);
                return std::make_shared<DistanceTest>(settings);
            }
            if (test.type == "Dot")
            {
                DotProductTest::Settings settings;
                settings.direction = stringOf(p, "direction", "forward") == "toTarget"
                    ? DotProductTest::Direction::ToTarget
                    : DotProductTest::Direction::Forward;
                return std::make_shared<DotProductTest>(settings);
            }
            if (test.type == "OnNavMesh")
            {
                OnNavMeshTest::Settings settings;
                settings.tolerance = floatOf(p, "tolerance", settings.tolerance);
                return std::make_shared<OnNavMeshTest>(settings);
            }
            if (test.type == "PathCost")
            {
                PathfindingCostTest::Settings settings;
                settings.from = contextPointOf(p, "from");
                return std::make_shared<PathfindingCostTest>(settings);
            }
            if (test.type == "Trace")
            {
                TraceTest::Settings settings;
                settings.from = contextPointOf(p, "from");
                settings.eyeHeight = floatOf(p, "eyeHeight", settings.eyeHeight);
                settings.candidateHeight = floatOf(p, "candidateHeight", settings.candidateHeight);
                return std::make_shared<TraceTest>(settings);
            }
            return nullptr;
        }

        json paramsToJson(const EQSParamMap& params)
        {
            json object = json::object();
            for (const auto& param : params)
            {
                const std::string& name = param.first;
                std::visit([&object, &name](const auto& v)
                {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, float>)
                        object[name] = floatToJson(v);
                    else
                        object[name] = v;
                }, param.second);
            }
            return object;
        }

        std::string aroundText(const EQSParamMap& params)
        {
            return contextPointOf(params, "center") == EQSContextPoint::Target ? "around target" : "around querier";
        }
    }

    // ---- Catalogue ----

    const std::vector<EQSGeneratorTypeSpec>& generatorTypes()
    {
        static const std::vector<EQSGeneratorTypeSpec> types = []
        {
            std::vector<EQSGeneratorTypeSpec> list;
            list.push_back(EQSGeneratorTypeSpec{"Ring", {
                contextPointParam("center", "Center the rings on the querier or on the query's target."),
                floatParam("radius", 10.0f, 0.1f, 500.0f, "Radius of the innermost ring (m)."),
                intParam("pointCount", 12, 1, 256, "Evenly spaced points per ring."),
                intParam("ringCount", 1, 1, 16, "Number of rings."),
                floatParam("ringSpacing", 5.0f, 0.1f, 100.0f, "Radius added per further ring (m).")
            }, "Concentric rings of evenly spaced points around the querier or the target, at its height."});
            list.push_back(EQSGeneratorTypeSpec{"Grid", {
                contextPointParam("center", "Center the grid on the querier or on the query's target."),
                intParam("size", 11, 1, 31, "Points per side of the square grid."),
                floatParam("spacing", 2.0f, 0.1f, 50.0f, "Distance between neighbouring points (m).")
            }, "A square grid of points centered on the querier or the target, at its height."});
            list.push_back(EQSGeneratorTypeSpec{"NavMeshPoints", {
                contextPointParam("center", "Center the disc on the querier or on the query's target."),
                floatParam("radius", 20.0f, 0.1f, 500.0f, "Radius of the disc the points are sampled in (m)."),
                intParam("pointCount", 20, 1, 256, "Points sampled (those with no navmesh nearby are dropped)."),
                floatParam("snapRadius", 2.0f, 0.1f, 10.0f, "How far a sample may be from the navmesh (m).")
            }, "Random points in a disc around the querier or the target, moved onto the navmesh; the same seed "
               "gives the same points."});
            list.push_back(EQSGeneratorTypeSpec{"Entities", {
                contextPointParam("center", "Search around the querier or around the query's target."),
                floatParam("radius", 25.0f, 0.1f, 1000.0f, "Search radius (m)."),
                stringParam("name", true, "Entity name to match (NameComponent)."),
                enumParam("match", {"exact", "prefix"}, "exact",
                    "exact: names equal to name; prefix: names that start with it."),
                boolParam("includeInactive", false, "Also return inactive entities."),
                intParam("maxCount", 64, 1, 256, "At most this many entities, the nearest ones.")
            }, "The entities with a matching name within radius, nearest first (never the querier); each "
               "candidate carries its entity."});
            return list;
        }();
        return types;
    }

    const std::vector<EQSTestTypeSpec>& testTypes()
    {
        static const std::vector<EQSTestTypeSpec> types = []
        {
            std::vector<EQSTestTypeSpec> list;
            list.push_back(EQSTestTypeSpec{"Distance", {
                contextPointParam("to", "Measure to the querier or to the query's target."),
                boolParam("horizontal", true, "Ignore height (XZ distance).")
            }, false, "metres from the candidate to the reference point",
               "Distance to the querier or the target. Score InverseLinear = nearest first."});
            list.push_back(EQSTestTypeSpec{"Dot", {
                enumParam("direction", {"forward", "toTarget"}, "forward",
                    "forward: the querier's facing; toTarget: the direction from the querier to the target.")
            }, false, "0..1 on the XZ plane: 1 straight along the direction from the querier, 0.5 sideways, 0 behind",
               "How well the direction from the querier to the candidate lines up with the querier's facing (or "
               "with the direction to the target)."});
            list.push_back(EQSTestTypeSpec{"OnNavMesh", {
                floatParam("tolerance", 2.0f, 0.1f, 10.0f, "How far from the navmesh still counts as on it (m).")
            }, true, "1 on the navmesh, 0 off it",
               "Whether the candidate is on the navmesh."});
            list.push_back(EQSTestTypeSpec{"PathCost", {
                contextPointParam("from", "Path from the querier or from the query's target.")
            }, false, "navmesh path length in metres (none when unreachable: a filter drops it, a score ranks it last)",
               "Walking distance from the querier or the target, for an agent of the querier's size."});
            list.push_back(EQSTestTypeSpec{"Trace", {
                contextPointParam("from", "Look from the querier or from the query's target."),
                floatParam("eyeHeight", 1.6f, 0.0f, 5.0f, "Eye height above the looking point (m)."),
                floatParam("candidateHeight", 1.0f, 0.0f, 5.0f, "Height above the candidate the ray aims at (m).")
            }, true, "1 visible (nothing blocks the ray), 0 occluded",
               "Line of sight from the querier's or the target's eye to the candidate; the looking entity's and "
               "the candidate entity's own colliders never block. filter {} = visible, filter {\"max\": 0} = hidden."});
            return list;
        }();
        return types;
    }

    const EQSGeneratorTypeSpec* findGeneratorType(std::string_view type)
    {
        for (const EQSGeneratorTypeSpec& spec : generatorTypes())
        {
            if (equalsIgnoreCase(spec.type, type))
                return &spec;
        }
        return nullptr;
    }

    const EQSTestTypeSpec* findTestType(std::string_view type)
    {
        for (const EQSTestTypeSpec& spec : testTypes())
        {
            if (equalsIgnoreCase(spec.type, type))
                return &spec;
        }
        return nullptr;
    }

    // ---- Parse ----

    EQSSpecParseResult parseQuerySpec(const json& document)
    {
        EQSSpecParseResult result;
        std::vector<EQSSpecError>& errors = result.errors;
        if (!document.is_object())
        {
            addError(errors, "", std::format("an EQS query template must be a JSON object (got {})", document.type_name()));
            return result;
        }
        checkKeys(document, {"version", "description", "generator", "tests", "maxResults"}, "", errors);

        EQSQuerySpec spec;

        if (auto it = document.find("version"); it != document.end())
        {
            const std::optional<int64_t> version = integralValue(*it);
            if (!version || *version != static_cast<int64_t>(kQuerySpecVersion))
                addError(errors, "version", std::format("must be {} (got {})", kQuerySpecVersion, it->dump()));
        }

        if (auto it = document.find("description"); it != document.end())
        {
            if (it->is_string())
                spec.description = it->get<std::string>();
            else
                addError(errors, "description", std::format("must be a string (got {})", it->type_name()));
        }

        // Generator
        const EQSGeneratorTypeSpec* generatorType = nullptr;
        bool generatorParamsValid = false;
        auto generatorIt = document.find("generator");
        if (generatorIt == document.end())
        {
            addError(errors, "generator", "missing required key 'generator'");
        }
        else if (!generatorIt->is_object())
        {
            addError(errors, "generator", std::format("must be an object (got {})", generatorIt->type_name()));
        }
        else
        {
            checkKeys(*generatorIt, {"type", "params"}, "generator", errors);
            auto typeIt = generatorIt->find("type");
            if (typeIt == generatorIt->end())
            {
                addError(errors, "generator.type", "missing required key 'type'");
            }
            else if (!typeIt->is_string())
            {
                addError(errors, "generator.type", std::format("must be a string (got {})", typeIt->type_name()));
            }
            else
            {
                generatorType = findGeneratorType(typeIt->get<std::string>());
                if (!generatorType)
                {
                    std::vector<std::string> names;
                    for (const EQSGeneratorTypeSpec& known : generatorTypes())
                        names.push_back(known.type);
                    addError(errors, "generator.type", std::format("unknown generator type '{}' (expected {})",
                                                                   typeIt->get<std::string>(), joinNames(names)));
                }
            }

            if (generatorType)
            {
                spec.generator.type = generatorType->type;
                const size_t errorsBefore = errors.size();
                auto paramsIt = generatorIt->find("params");
                parseParams(generatorType->params, generatorType->type,
                            paramsIt == generatorIt->end() ? nullptr : &*paramsIt, "generator.params",
                            spec.generator.params, errors);
                generatorParamsValid = errors.size() == errorsBefore;
            }
        }

        // Tests
        bool hasPathCost = false;
        if (auto testsIt = document.find("tests"); testsIt != document.end())
        {
            if (!testsIt->is_array())
            {
                addError(errors, "tests", std::format("must be an array (got {})", testsIt->type_name()));
            }
            else
            {
                for (size_t i = 0; i < testsIt->size(); ++i)
                {
                    const std::string testPath = std::format("tests[{}]", i);
                    const json& test = (*testsIt)[i];
                    if (!test.is_object())
                    {
                        addError(errors, testPath, std::format("must be an object (got {})", test.type_name()));
                        continue;
                    }
                    checkKeys(test, {"type", "params", "filter", "score"}, testPath, errors);

                    EQSTestDecl decl;
                    const EQSTestTypeSpec* testType = nullptr;
                    auto typeIt = test.find("type");
                    if (typeIt == test.end())
                    {
                        addError(errors, testPath + ".type", "missing required key 'type'");
                    }
                    else if (!typeIt->is_string())
                    {
                        addError(errors, testPath + ".type", std::format("must be a string (got {})", typeIt->type_name()));
                    }
                    else
                    {
                        testType = findTestType(typeIt->get<std::string>());
                        if (!testType)
                        {
                            std::vector<std::string> names;
                            for (const EQSTestTypeSpec& known : testTypes())
                                names.push_back(known.type);
                            addError(errors, testPath + ".type", std::format("unknown test type '{}' (expected {})",
                                                                             typeIt->get<std::string>(), joinNames(names)));
                        }
                    }

                    if (testType)
                    {
                        decl.type = testType->type;
                        if (testType->type == "PathCost")
                            hasPathCost = true;
                        auto paramsIt = test.find("params");
                        parseParams(testType->params, testType->type, paramsIt == test.end() ? nullptr : &*paramsIt,
                                    testPath + ".params", decl.params, errors);
                    }

                    auto filterIt = test.find("filter");
                    auto scoreIt = test.find("score");
                    const bool hasFilter = filterIt != test.end();
                    const bool hasScore = scoreIt != test.end();
                    if (hasFilter && hasScore)
                        addError(errors, testPath, "has both 'filter' and 'score' (a test either filters or scores)");
                    else if (!hasFilter && !hasScore)
                        addError(errors, testPath, "needs 'filter' or 'score'");
                    else if (hasFilter)
                        parseFilter(*filterIt, testPath + ".filter", testType && testType->boolean, decl.config, errors);
                    else
                        parseScore(*scoreIt, testPath + ".score", decl.config, errors);

                    spec.tests.push_back(std::move(decl));
                }
            }
        }

        if (auto it = document.find("maxResults"); it != document.end())
        {
            const std::optional<int64_t> maxResults = integralValue(*it);
            if (!maxResults || *maxResults < 1 || *maxResults > static_cast<int64_t>(kMaxResultsLimit))
                addError(errors, "maxResults", std::format("must be a whole number from 1 to {} (got {})", kMaxResultsLimit, it->dump()));
            else
                spec.maxResults = static_cast<uint32_t>(*maxResults);
        }

        if (generatorType && generatorParamsValid)
        {
            const uint64_t output = maxGeneratorOutput(spec.generator);
            const uint32_t limit = hasPathCost ? kMaxCandidatesWithPathCost : kMaxCandidates;
            if (output > limit)
            {
                addError(errors, "generator.params",
                         std::format("the {} generator can produce {} candidates; the limit is {}{}", spec.generator.type,
                                     output, limit, hasPathCost ? " with a PathCost test (one path search per candidate)" : ""));
            }
        }

        if (errors.empty())
            result.spec = std::move(spec);
        return result;
    }

    EQSSpecParseResult parseQuerySpecText(std::string_view text)
    {
        json document;
        try
        {
            document = json::parse(text.begin(), text.end());
        }
        catch (const json::parse_error& error)
        {
            EQSSpecParseResult result;
            addError(result.errors, "", std::format("not valid JSON (byte {}): {}", error.byte, error.what()));
            return result;
        }
        return parseQuerySpec(document);
    }

    // ---- Emit ----

    json querySpecToJson(const EQSQuerySpec& spec)
    {
        json document = json::object();
        document["version"] = kQuerySpecVersion;
        document["description"] = spec.description;

        json generator = json::object();
        generator["type"] = spec.generator.type;
        generator["params"] = paramsToJson(spec.generator.params);
        document["generator"] = std::move(generator);

        json tests = json::array();
        for (const EQSTestDecl& test : spec.tests)
        {
            json entry = json::object();
            entry["type"] = test.type;
            entry["params"] = paramsToJson(test.params);
            if (test.config.purpose == EQSTestPurpose::Filter)
            {
                json filter = json::object();
                if (test.config.filterMin)
                    filter["min"] = floatToJson(*test.config.filterMin);
                if (test.config.filterMax)
                    filter["max"] = floatToJson(*test.config.filterMax);
                entry["filter"] = std::move(filter);
            }
            else
            {
                json score = json::object();
                score["mode"] = std::string(scoringModeName(test.config.mode));
                score["weight"] = floatToJson(test.config.weight);
                score["combine"] = std::string(scoreCombineName(test.config.combine));
                entry["score"] = std::move(score);
            }
            tests.push_back(std::move(entry));
        }
        document["tests"] = std::move(tests);
        document["maxResults"] = spec.maxResults;
        return document;
    }

    std::string querySpecToText(const EQSQuerySpec& spec)
    {
        return querySpecToJson(spec).dump(4);
    }

    // ---- Build ----

    EQSQueryDef buildQuery(const EQSQuerySpec& spec, const std::string& name)
    {
        EQSQueryDef def;
        def.name = name;
        def.maxResults = spec.maxResults;
        def.generator = makeGenerator(spec.generator);
        def.tests.reserve(spec.tests.size());
        for (const EQSTestDecl& test : spec.tests)
            def.tests.push_back(EQSTestEntry{makeTest(test), test.config});
        return def;
    }

    // ---- Describe ----

    std::string summarizeQuery(const EQSQuerySpec& spec)
    {
        const EQSParamMap& p = spec.generator.params;
        std::string text;
        if (spec.generator.type == "Ring")
        {
            text = std::format("Ring(radius {}, {} x {}, {})", floatOf(p, "radius", 0.0f), intOf(p, "pointCount", 0),
                               intOf(p, "ringCount", 0), aroundText(p));
        }
        else if (spec.generator.type == "Grid")
        {
            text = std::format("Grid({} x {}, spacing {}, {})", intOf(p, "size", 0), intOf(p, "size", 0),
                               floatOf(p, "spacing", 0.0f), aroundText(p));
        }
        else if (spec.generator.type == "NavMeshPoints")
        {
            text = std::format("NavMeshPoints({} within {}, {})", intOf(p, "pointCount", 0), floatOf(p, "radius", 0.0f),
                               aroundText(p));
        }
        else if (spec.generator.type == "Entities")
        {
            text = std::format("Entities(name {}{}, radius {}, {})", stringOf(p, "name", ""),
                               stringOf(p, "match", "exact") == "prefix" ? "*" : "", floatOf(p, "radius", 0.0f),
                               aroundText(p));
        }
        else
        {
            text = spec.generator.type;
        }

        for (const EQSTestDecl& test : spec.tests)
        {
            if (test.config.purpose == EQSTestPurpose::Filter)
            {
                text += " | filter " + test.type;
                if (test.config.filterMin || test.config.filterMax)
                {
                    text += " [";
                    text += test.config.filterMin ? std::format("{}", *test.config.filterMin) : std::string("-inf");
                    text += "..";
                    text += test.config.filterMax ? std::format("{}", *test.config.filterMax) : std::string("inf");
                    text += "]";
                }
            }
            else
            {
                text += std::format(" | score {} {}", test.type, scoringModeName(test.config.mode));
            }
        }
        text += std::format(" | {} result{}", spec.maxResults, spec.maxResults == 1 ? "" : "s");
        return text;
    }

    bool isValidQueryName(std::string_view name)
    {
        if (name.empty() || name.size() > 64)
            return false;
        auto isLetter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
        auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
        if (!isLetter(name.front()))
            return false;
        return std::all_of(name.begin() + 1, name.end(),
                           [&](char c) { return isLetter(c) || isDigit(c) || c == '_'; });
    }
}
