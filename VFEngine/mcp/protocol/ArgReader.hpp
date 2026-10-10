#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mcp
{
    // Thrown for a missing / mistyped tool argument. McpServer reports it as a
    // tool execution error (isError: true) so the model can self-correct.
    class ArgError : public std::invalid_argument
    {
    public:
        using std::invalid_argument::invalid_argument;
    };

    // Typed accessors over a tools/call `arguments` object.
    class ArgReader
    {
    public:
        explicit ArgReader(const nlohmann::json& arguments)
            : args(arguments.is_object() ? arguments : emptyObject())
        {
        }

        bool has(const char* name) const
        {
            auto it = args.find(name);
            return it != args.end() && !it->is_null();
        }

        const nlohmann::json& raw(const char* name) const
        {
            auto it = args.find(name);
            if (it == args.end() || it->is_null())
            {
                throw ArgError(std::string("missing required argument '") + name + "'");
            }
            return *it;
        }

        std::string requireString(const char* name) const
        {
            const nlohmann::json& v = raw(name);
            if (!v.is_string())
            {
                throw ArgError(std::string("argument '") + name + "' must be a string");
            }
            return v.get<std::string>();
        }

        std::string optString(const char* name, std::string fallback = {}) const
        {
            return has(name) ? requireString(name) : std::move(fallback);
        }

        int64_t requireInt(const char* name) const
        {
            const nlohmann::json& v = raw(name);
            if (v.is_number_integer() || v.is_number_unsigned())
            {
                return v.get<int64_t>();
            }
            // Models frequently send 3.0 for 3; accept integral floats.
            if (v.is_number_float())
            {
                double d = v.get<double>();
                if (d == static_cast<double>(static_cast<int64_t>(d)))
                {
                    return static_cast<int64_t>(d);
                }
            }
            throw ArgError(std::string("argument '") + name + "' must be an integer");
        }

        int64_t optInt(const char* name, int64_t fallback) const
        {
            return has(name) ? requireInt(name) : fallback;
        }

        double requireNumber(const char* name) const
        {
            const nlohmann::json& v = raw(name);
            if (!v.is_number())
            {
                throw ArgError(std::string("argument '") + name + "' must be a number");
            }
            return v.get<double>();
        }

        double optNumber(const char* name, double fallback) const
        {
            return has(name) ? requireNumber(name) : fallback;
        }

        bool requireBool(const char* name) const
        {
            const nlohmann::json& v = raw(name);
            if (!v.is_boolean())
            {
                throw ArgError(std::string("argument '") + name + "' must be a boolean");
            }
            return v.get<bool>();
        }

        bool optBool(const char* name, bool fallback) const
        {
            return has(name) ? requireBool(name) : fallback;
        }

        // Entity ids are the raw entt::entity value (uint32).
        uint32_t requireEntity(const char* name) const
        {
            int64_t value = requireInt(name);
            if (value < 0 || value > static_cast<int64_t>(UINT32_MAX))
            {
                throw ArgError(std::string("argument '") + name + "' is not a valid entity id");
            }
            return static_cast<uint32_t>(value);
        }

        std::optional<uint32_t> optEntity(const char* name) const
        {
            if (!has(name))
            {
                return std::nullopt;
            }
            return requireEntity(name);
        }

        // [x, y, z] (also accepts {"x":..,"y":..,"z":..}).
        glm::vec3 requireVec3(const char* name) const
        {
            return toVec3(raw(name), name);
        }

        std::optional<glm::vec3> optVec3(const char* name) const
        {
            if (!has(name))
            {
                return std::nullopt;
            }
            return requireVec3(name);
        }

        std::vector<std::string> requireStringArray(const char* name) const
        {
            const nlohmann::json& v = raw(name);
            if (!v.is_array())
            {
                throw ArgError(std::string("argument '") + name + "' must be an array of strings");
            }
            std::vector<std::string> out;
            out.reserve(v.size());
            for (const auto& item : v)
            {
                if (!item.is_string())
                {
                    throw ArgError(std::string("argument '") + name + "' must be an array of strings");
                }
                out.push_back(item.get<std::string>());
            }
            return out;
        }

        // One of `allowed`, matched case-insensitively and returned in its listed spelling. The
        // error lists every allowed value so the model can correct itself in one step.
        std::string requireEnum(const char* name, const std::vector<std::string>& allowed) const
        {
            const std::string value = requireString(name);
            for (const std::string& candidate : allowed)
            {
                if (equalsIgnoreCase(value, candidate))
                {
                    return candidate;
                }
            }
            throw ArgError(std::string("argument '") + name + "' must be one of " + joinValues(allowed) +
                           " (got '" + value + "')");
        }

        std::string optEnum(const char* name, const std::vector<std::string>& allowed, std::string fallback) const
        {
            return has(name) ? requireEnum(name, allowed) : std::move(fallback);
        }

        // A finite number in [min, max]; the error names the range.
        double requireNumberInRange(const char* name, double min, double max) const
        {
            const double value = requireNumber(name);
            if (!std::isfinite(value) || value < min || value > max)
            {
                throw ArgError(std::string("argument '") + name + "' must be between " + formatNumber(min) +
                               " and " + formatNumber(max) + " (got " + formatNumber(value) + ")");
            }
            return value;
        }

        double optNumberInRange(const char* name, double min, double max, double fallback) const
        {
            return has(name) ? requireNumberInRange(name, min, max) : fallback;
        }

        // An integer in [min, max]; the error names the range.
        int64_t requireIntInRange(const char* name, int64_t min, int64_t max) const
        {
            const int64_t value = requireInt(name);
            if (value < min || value > max)
            {
                throw ArgError(std::string("argument '") + name + "' must be between " + std::to_string(min) +
                               " and " + std::to_string(max) + " (got " + std::to_string(value) + ")");
            }
            return value;
        }

        int64_t optIntInRange(const char* name, int64_t min, int64_t max, int64_t fallback) const
        {
            return has(name) ? requireIntInRange(name, min, max) : fallback;
        }

        // VK-1653: a world ground position for the terrain tools, returned as (x, z). Accepts
        // [x, z], [x, y, z] (y ignored) and {"x":..,"z":..}.
        glm::vec2 requireGroundPoint(const char* name) const
        {
            return toGroundPoint(raw(name), name);
        }

        // An array of minCount..maxCount ground points; element errors name 'name[i]'.
        std::vector<glm::vec2> requireGroundPoints(const char* name, std::size_t minCount, std::size_t maxCount) const
        {
            const nlohmann::json& v = raw(name);
            if (!v.is_array())
            {
                throw ArgError(std::string("argument '") + name + "' must be an array of [x, z] points");
            }
            if (v.size() < minCount || v.size() > maxCount)
            {
                throw ArgError(std::string("argument '") + name + "' must hold " + std::to_string(minCount) + "-" +
                               std::to_string(maxCount) + " points (got " + std::to_string(v.size()) + ")");
            }
            std::vector<glm::vec2> points;
            points.reserve(v.size());
            for (std::size_t i = 0; i < v.size(); ++i)
            {
                points.push_back(toGroundPoint(v[i], std::string(name) + "[" + std::to_string(i) + "]"));
            }
            return points;
        }

        // Largest accepted |coordinate| of a ground point, in metres.
        static constexpr double maxGroundCoordinate = 1.0e6;

        // Coordinates are validated as doubles BEFORE narrowing to float: 1e39 is a finite double
        // but an infinite float, and must be an argument error rather than reach the engine.
        static glm::vec2 toGroundPoint(const nlohmann::json& v, const std::string& name)
        {
            const nlohmann::json* x = nullptr;
            const nlohmann::json* y = nullptr;
            const nlohmann::json* z = nullptr;
            if (v.is_array() && (v.size() == 2 || v.size() == 3))
            {
                x = &v[0];
                y = v.size() == 3 ? &v[1] : nullptr;
                z = &v[v.size() - 1];
            }
            else if (v.is_object() && v.contains("x") && v.contains("z"))
            {
                x = &v.at("x");
                y = v.contains("y") ? &v.at("y") : nullptr;
                z = &v.at("z");
            }
            if (x == nullptr || z == nullptr || !x->is_number() || !z->is_number() ||
                (y != nullptr && !y->is_number()))
            {
                throw ArgError("argument '" + name + "' must be a ground point [x, z] (or [x, y, z] with y ignored)");
            }
            return {groundCoordinate(*x, name), groundCoordinate(*z, name)};
        }

        static glm::vec3 toVec3(const nlohmann::json& v, const char* name)
        {
            if (v.is_array() && v.size() == 3 && v[0].is_number() && v[1].is_number() && v[2].is_number())
            {
                return {v[0].get<float>(), v[1].get<float>(), v[2].get<float>()};
            }
            if (v.is_object() && v.contains("x") && v.contains("y") && v.contains("z") &&
                v["x"].is_number() && v["y"].is_number() && v["z"].is_number())
            {
                return {v["x"].get<float>(), v["y"].get<float>(), v["z"].get<float>()};
            }
            throw ArgError(std::string("argument '") + name + "' must be [x, y, z]");
        }

    private:
        static const nlohmann::json& emptyObject()
        {
            static const nlohmann::json empty = nlohmann::json::object();
            return empty;
        }

        static bool equalsIgnoreCase(const std::string& a, const std::string& b)
        {
            if (a.size() != b.size())
            {
                return false;
            }
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                {
                    return false;
                }
            }
            return true;
        }

        static std::string joinValues(const std::vector<std::string>& values)
        {
            std::string out;
            for (const std::string& value : values)
            {
                out += out.empty() ? value : ", " + value;
            }
            return out;
        }

        // JSON spelling: 0.1 stays "0.1" rather than std::to_string's "0.100000".
        static std::string formatNumber(double value)
        {
            return nlohmann::json(value).dump();
        }

        static float groundCoordinate(const nlohmann::json& value, const std::string& name)
        {
            const double coordinate = value.get<double>();
            if (!std::isfinite(coordinate) || std::fabs(coordinate) > maxGroundCoordinate)
            {
                throw ArgError("argument '" + name + "' has coordinate " + formatNumber(coordinate) +
                               ", outside +/-" + formatNumber(maxGroundCoordinate) + " metres");
            }
            return static_cast<float>(coordinate);
        }

        const nlohmann::json& args;
    };

    inline nlohmann::json vec3ToJson(const glm::vec3& v)
    {
        return nlohmann::json::array({v.x, v.y, v.z});
    }

    // Small JSON-Schema builders so tool definitions stay readable.
    namespace schema
    {
        inline nlohmann::json string(std::string description)
        {
            return {{"type", "string"}, {"description", std::move(description)}};
        }

        inline nlohmann::json integer(std::string description)
        {
            return {{"type", "integer"}, {"description", std::move(description)}};
        }

        inline nlohmann::json number(std::string description)
        {
            return {{"type", "number"}, {"description", std::move(description)}};
        }

        inline nlohmann::json boolean(std::string description)
        {
            return {{"type", "boolean"}, {"description", std::move(description)}};
        }

        inline nlohmann::json entity(std::string description = "Entity id (from scene_get_hierarchy / entity_find / entity_create)")
        {
            return {{"type", "integer"}, {"minimum", 0}, {"description", std::move(description)}};
        }

        inline nlohmann::json vec3(std::string description)
        {
            return {
                {"type", "array"},
                {"items", {{"type", "number"}}},
                {"minItems", 3},
                {"maxItems", 3},
                {"description", std::move(description)}
            };
        }

        inline nlohmann::json enumString(std::string description, const std::vector<std::string>& values)
        {
            return {{"type", "string"}, {"enum", values}, {"description", std::move(description)}};
        }

        inline nlohmann::json array(nlohmann::json items, std::string description)
        {
            return {{"type", "array"}, {"items", std::move(items)}, {"description", std::move(description)}};
        }

        // A number with inclusive bounds.
        inline nlohmann::json numberRange(std::string description, double minimum, double maximum)
        {
            return {
                {"type", "number"},
                {"minimum", minimum},
                {"maximum", maximum},
                {"description", std::move(description)}
            };
        }

        // An integer with inclusive bounds.
        inline nlohmann::json integerRange(std::string description, int64_t minimum, int64_t maximum)
        {
            return {
                {"type", "integer"},
                {"minimum", minimum},
                {"maximum", maximum},
                {"description", std::move(description)}
            };
        }

        // VK-1653: a world ground position [x, z] in metres for the terrain tools ([x, y, z] is
        // accepted with y ignored; ArgReader::toGroundPoint also takes {"x":..,"z":..}).
        inline nlohmann::json groundPoint(std::string description)
        {
            return {
                {"type", "array"},
                {"items", {{"type", "number"}}},
                {"minItems", 2},
                {"maxItems", 3},
                {"description", std::move(description)}
            };
        }

        // An array of minItems..maxItems ground points.
        inline nlohmann::json groundPoints(std::string description, std::size_t minItems, std::size_t maxItems)
        {
            nlohmann::json s = array(groundPoint("[x, z] in world metres"), std::move(description));
            s["minItems"] = minItems;
            s["maxItems"] = maxItems;
            return s;
        }

        inline nlohmann::json anyObject(std::string description)
        {
            return {{"type", "object"}, {"description", std::move(description)}};
        }

        inline nlohmann::json object(nlohmann::json properties, std::vector<std::string> required = {})
        {
            nlohmann::json s{{"type", "object"}, {"properties", std::move(properties)}};
            if (!required.empty())
            {
                s["required"] = std::move(required);
            }
            return s;
        }
    }
}
