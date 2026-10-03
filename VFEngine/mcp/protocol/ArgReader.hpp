#pragma once

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
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
