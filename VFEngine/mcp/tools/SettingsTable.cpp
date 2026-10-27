#include "SettingsTable.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <system_error>

namespace mcp::tools::env
{
    namespace detail
    {
        // ------------------------------------------------------------------
        // Text
        // ------------------------------------------------------------------

        std::string floatText(float value)
        {
            return floatJson(value).dump();
        }

        std::string valueText(const nlohmann::json& value)
        {
            return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        }

        std::string pathOf(std::string_view prefix, std::string_view key)
        {
            return prefix.empty() ? std::string(key) : std::string(prefix) + "." + std::string(key);
        }

        bool equalsIgnoreCase(std::string_view a, std::string_view b)
        {
            return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y)
            {
                return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
            });
        }

        std::string rangeError(std::string_view path, double min, double max, const nlohmann::json& value)
        {
            return "'" + std::string(path) + "' must be between " + floatText(static_cast<float>(min)) + " and " +
                   floatText(static_cast<float>(max)) + " (got " + valueText(value) + ")";
        }

        // ------------------------------------------------------------------
        // Validation: every check throws ArgError naming `path`
        // ------------------------------------------------------------------

        int64_t checkedInteger(const nlohmann::json& value, double min, double max, std::string_view path)
        {
            double number = 0.0;
            if (value.is_number_unsigned())
            {
                number = static_cast<double>(value.get<uint64_t>());
            }
            else if (value.is_number_integer())
            {
                number = static_cast<double>(value.get<int64_t>());
            }
            else if (value.is_number_float() && std::isfinite(value.get<double>()) &&
                     value.get<double>() == std::trunc(value.get<double>()))
            {
                number = value.get<double>(); // models frequently send 3.0 for 3
            }
            else
            {
                throw ArgError("'" + std::string(path) + "' must be an integer (got " + valueText(value) + ")");
            }
            if (number < min || number > max)
            {
                throw ArgError("'" + std::string(path) + "' must be between " + std::to_string(static_cast<int64_t>(min)) +
                               " and " + std::to_string(static_cast<int64_t>(max)) + " (got " + valueText(value) + ")");
            }
            return static_cast<int64_t>(number);
        }

        std::string_view checkedEnum(const nlohmann::json& value, std::span<const std::string_view> names,
                                     std::string_view path)
        {
            if (value.is_string())
            {
                const std::string text = value.get<std::string>();
                for (std::string_view name : names)
                {
                    if (equalsIgnoreCase(text, name))
                    {
                        return name;
                    }
                }
            }
            throw ArgError("'" + std::string(path) + "' must be one of " + joined(names) + " (got " + valueText(value) + ")");
        }

        nlohmann::json canonicalValue(FieldKind kind, double min, double max, std::span<const std::string_view> names,
                                      const nlohmann::json& value, std::string_view path)
        {
            switch (kind)
            {
            case FieldKind::Bool:
                return requireBool(value, path);
            case FieldKind::Float:
                return static_cast<double>(requireFloat(value, min, max, path));
            case FieldKind::Int:
            case FieldKind::UInt:
                return checkedInteger(value, min, max, path);
            case FieldKind::Vec3:
            {
                const glm::vec3 vector = requireVec3(value, min, max, path);
                return nlohmann::json::array({static_cast<double>(vector.x), static_cast<double>(vector.y),
                                              static_cast<double>(vector.z)});
            }
            case FieldKind::Enum:
                return std::string(checkedEnum(value, names, path));
            case FieldKind::String:
                return requireString(value, path);
            case FieldKind::Vec2:
            {
                const glm::vec2 vector = requireVec2(value, min, max, path);
                return nlohmann::json::array({static_cast<double>(vector.x), static_cast<double>(vector.y)});
            }
            case FieldKind::Vec4:
            {
                const glm::vec4 vector = requireVec4(value, min, max, path);
                return nlohmann::json::array({static_cast<double>(vector.x), static_cast<double>(vector.y),
                                              static_cast<double>(vector.z), static_cast<double>(vector.w)});
            }
            case FieldKind::Color:
            {
                const glm::vec4 color = requireColor(value, min, max, path);
                return nlohmann::json::array({static_cast<double>(color.r), static_cast<double>(color.g),
                                              static_cast<double>(color.b), static_cast<double>(color.a)});
            }
            case FieldKind::StringList:
                return requireStringList(value, min, max, path);
            }
            throw std::logic_error("unknown field kind");
        }

        // ------------------------------------------------------------------
        // Reporting
        // ------------------------------------------------------------------

        nlohmann::json namesJson(std::span<const std::string_view> names)
        {
            nlohmann::json out = nlohmann::json::array();
            for (std::string_view name : names)
            {
                out.push_back(std::string(name));
            }
            return out;
        }

        nlohmann::json rangeEntry(FieldKind kind, double min, double max, std::span<const std::string_view> names)
        {
            switch (kind)
            {
            case FieldKind::Bool:
                return {{"type", "boolean"}};
            // Float bounds are reported as the floats values are compared against (see rangeError).
            case FieldKind::Float:
                return {{"type", "number"}, {"min", floatJson(static_cast<float>(min))},
                        {"max", floatJson(static_cast<float>(max))}};
            case FieldKind::Int:
            case FieldKind::UInt:
                return {{"type", "integer"}, {"min", static_cast<int64_t>(min)}, {"max", static_cast<int64_t>(max)}};
            case FieldKind::Vec3:
                return {{"type", "vec3"}, {"min", floatJson(static_cast<float>(min))},
                        {"max", floatJson(static_cast<float>(max))}};
            case FieldKind::Enum:
                return {{"type", "enum"}, {"values", namesJson(names)}};
            case FieldKind::String:
                return {{"type", "string"}};
            case FieldKind::Vec2:
                return {{"type", "vec2"}, {"min", floatJson(static_cast<float>(min))},
                        {"max", floatJson(static_cast<float>(max))}};
            case FieldKind::Vec4:
                return {{"type", "vec4"}, {"min", floatJson(static_cast<float>(min))},
                        {"max", floatJson(static_cast<float>(max))}};
            case FieldKind::Color:
                return {{"type", "color"}, {"min", floatJson(static_cast<float>(min))},
                        {"max", floatJson(static_cast<float>(max))}};
            case FieldKind::StringList:
                return {{"type", "stringList"}, {"minItems", static_cast<int64_t>(min)},
                        {"maxItems", static_cast<int64_t>(max)}};
            }
            throw std::logic_error("unknown field kind");
        }

        nlohmann::json fieldSchema(FieldKind kind, double min, double max, std::span<const std::string_view> names,
                                   std::string description)
        {
            switch (kind)
            {
            case FieldKind::Bool:
                return schema::boolean(std::move(description));
            case FieldKind::Float:
                return schema::numberRange(std::move(description), min, max);
            case FieldKind::Int:
            case FieldKind::UInt:
                return schema::integerRange(std::move(description), static_cast<int64_t>(min), static_cast<int64_t>(max));
            case FieldKind::Vec3:
            {
                nlohmann::json out = schema::vec3(std::move(description));
                out["items"] = {{"type", "number"}, {"minimum", min}, {"maximum", max}};
                return out;
            }
            case FieldKind::Enum:
            {
                std::vector<std::string> values;
                for (std::string_view name : names)
                {
                    values.emplace_back(name);
                }
                return schema::enumString(std::move(description), values);
            }
            case FieldKind::String:
                return schema::string(std::move(description));
            case FieldKind::Vec2:
            case FieldKind::Vec4:
            {
                const int64_t count = kind == FieldKind::Vec2 ? 2 : 4;
                return {
                    {"type", "array"},
                    {"items", {{"type", "number"}, {"minimum", min}, {"maximum", max}}},
                    {"minItems", count},
                    {"maxItems", count},
                    {"description", std::move(description)}
                };
            }
            case FieldKind::Color:
            {
                nlohmann::json components = {
                    {"type", "array"},
                    {"items", {{"type", "number"}, {"minimum", min}, {"maximum", max}}},
                    {"minItems", 3},
                    {"maxItems", 4}
                };
                nlohmann::json hex = {{"type", "string"}, {"pattern", "^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$"}};
                nlohmann::json out = nlohmann::json::object();
                out["anyOf"] = nlohmann::json::array({std::move(components), std::move(hex)});
                out["description"] = std::move(description);
                return out;
            }
            case FieldKind::StringList:
                return {
                    {"type", "array"},
                    {"items", {{"type", "string"}}},
                    {"minItems", static_cast<int64_t>(min)},
                    {"maxItems", static_cast<int64_t>(max)},
                    {"description", std::move(description)}
                };
            }
            throw std::logic_error("unknown field kind");
        }
    }

    // ------------------------------------------------------------------
    // Values and arguments
    // ------------------------------------------------------------------

    nlohmann::json floatJson(float value)
    {
        if (!std::isfinite(value))
        {
            return nullptr;
        }
        std::array<char, 64> text{};
        const std::to_chars_result written = std::to_chars(text.data(), text.data() + text.size(), value);
        double shortest = static_cast<double>(value);
        if (written.ec == std::errc{})
        {
            double parsed = 0.0;
            const std::from_chars_result read = std::from_chars(text.data(), written.ptr, parsed);
            if (read.ec == std::errc{})
            {
                shortest = parsed;
            }
        }
        return shortest;
    }

    nlohmann::json vec3Json(const glm::vec3& value)
    {
        return nlohmann::json::array({floatJson(value.x), floatJson(value.y), floatJson(value.z)});
    }

    nlohmann::json vec2Json(const glm::vec2& value)
    {
        return nlohmann::json::array({floatJson(value.x), floatJson(value.y)});
    }

    nlohmann::json vec4Json(const glm::vec4& value)
    {
        return nlohmann::json::array({floatJson(value.x), floatJson(value.y), floatJson(value.z), floatJson(value.w)});
    }

    float requireFloat(const nlohmann::json& value, double min, double max, std::string_view path)
    {
        if (!value.is_number())
        {
            throw ArgError("'" + std::string(path) + "' must be a number (got " + detail::valueText(value) + ")");
        }
        const double number = value.get<double>();
        // Validated as a double first: 1e39 is a finite double but would not fit a float.
        if (!std::isfinite(number) || std::fabs(number) > static_cast<double>(std::numeric_limits<float>::max()))
        {
            throw ArgError(detail::rangeError(path, min, max, value));
        }
        const float narrowed = static_cast<float>(number);
        if (narrowed < static_cast<float>(min) || narrowed > static_cast<float>(max))
        {
            throw ArgError(detail::rangeError(path, min, max, value));
        }
        return narrowed;
    }

    glm::vec3 requireVec3(const nlohmann::json& value, double min, double max, std::string_view path)
    {
        if (!value.is_array() || value.size() != 3)
        {
            throw ArgError("'" + std::string(path) + "' must be an array of 3 numbers (got " + detail::valueText(value) +
                           ")");
        }
        glm::vec3 out(0.0f);
        for (std::size_t i = 0; i < 3; ++i)
        {
            out[static_cast<glm::length_t>(i)] =
                requireFloat(value[i], min, max, std::string(path) + "[" + std::to_string(i) + "]");
        }
        return out;
    }

    glm::vec2 requireVec2(const nlohmann::json& value, double min, double max, std::string_view path)
    {
        if (!value.is_array() || value.size() != 2)
        {
            throw ArgError("'" + std::string(path) + "' must be an array of 2 numbers (got " + detail::valueText(value) +
                           ")");
        }
        glm::vec2 out(0.0f);
        for (std::size_t i = 0; i < 2; ++i)
        {
            out[static_cast<glm::length_t>(i)] =
                requireFloat(value[i], min, max, std::string(path) + "[" + std::to_string(i) + "]");
        }
        return out;
    }

    glm::vec4 requireVec4(const nlohmann::json& value, double min, double max, std::string_view path)
    {
        if (!value.is_array() || value.size() != 4)
        {
            throw ArgError("'" + std::string(path) + "' must be an array of 4 numbers (got " + detail::valueText(value) +
                           ")");
        }
        glm::vec4 out(0.0f);
        for (std::size_t i = 0; i < 4; ++i)
        {
            out[static_cast<glm::length_t>(i)] =
                requireFloat(value[i], min, max, std::string(path) + "[" + std::to_string(i) + "]");
        }
        return out;
    }

    glm::vec4 requireColor(const nlohmann::json& value, double min, double max, std::string_view path)
    {
        const auto shapeError = [&]()
        {
            return ArgError("'" + std::string(path) +
                            "' must be [r, g, b], [r, g, b, a] (0-1 each) or \"#RRGGBB\" / \"#RRGGBBAA\" (got " +
                            detail::valueText(value) + ")");
        };

        glm::vec4 out(1.0f);
        if (value.is_string())
        {
            const std::string text = value.get<std::string>();
            const bool hexShape = (text.size() == 7 || text.size() == 9) && text.front() == '#' &&
                                  std::all_of(text.begin() + 1, text.end(), [](char c)
                                  {
                                      return std::isxdigit(static_cast<unsigned char>(c)) != 0;
                                  });
            if (!hexShape)
            {
                throw shapeError();
            }
            const std::size_t channels = (text.size() - 1) / 2;
            for (std::size_t i = 0; i < channels; ++i)
            {
                unsigned int byte = 0;
                const char* first = text.data() + 1 + 2 * i;
                std::from_chars(first, first + 2, byte, 16); // two validated hex digits
                out[static_cast<glm::length_t>(i)] = static_cast<float>(byte) / 255.0f;
            }
        }
        else if (value.is_array() && (value.size() == 3 || value.size() == 4))
        {
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                out[static_cast<glm::length_t>(i)] =
                    requireFloat(value[i], min, max, std::string(path) + "[" + std::to_string(i) + "]");
            }
        }
        else
        {
            throw shapeError();
        }

        // A hex component, or the implied alpha of [r, g, b], must fit the range too.
        for (std::size_t i = 0; i < 4; ++i)
        {
            const float component = out[static_cast<glm::length_t>(i)];
            if (component < static_cast<float>(min) || component > static_cast<float>(max))
            {
                throw ArgError(detail::rangeError(std::string(path) + "[" + std::to_string(i) + "]", min, max,
                                                  floatJson(component)));
            }
        }
        return out;
    }

    std::vector<std::string> requireStringList(const nlohmann::json& value, double minCount, double maxCount,
                                               std::string_view path)
    {
        if (!value.is_array())
        {
            throw ArgError("'" + std::string(path) + "' must be an array of strings (got " + detail::valueText(value) + ")");
        }
        const double count = static_cast<double>(value.size());
        if (count < minCount || count > maxCount)
        {
            throw ArgError("'" + std::string(path) + "' must have " + std::to_string(static_cast<int64_t>(minCount)) +
                           " to " + std::to_string(static_cast<int64_t>(maxCount)) + " items (got " +
                           std::to_string(value.size()) + ")");
        }
        std::vector<std::string> out;
        out.reserve(value.size());
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            out.push_back(requireString(value[i], std::string(path) + "[" + std::to_string(i) + "]"));
        }
        return out;
    }

    bool requireBool(const nlohmann::json& value, std::string_view path)
    {
        if (!value.is_boolean())
        {
            throw ArgError("'" + std::string(path) + "' must be a boolean (got " + detail::valueText(value) + ")");
        }
        return value.get<bool>();
    }

    std::string requireString(const nlohmann::json& value, std::string_view path)
    {
        if (!value.is_string())
        {
            throw ArgError("'" + std::string(path) + "' must be a string (got " + detail::valueText(value) + ")");
        }
        return value.get<std::string>();
    }

    void rejectUnknownArgs(const nlohmann::json& args, std::initializer_list<std::string_view> allowed,
                           std::string_view toolName)
    {
        if (args.is_null())
        {
            return;
        }
        if (!args.is_object())
        {
            throw ArgError("the arguments of " + std::string(toolName) + " must be an object");
        }
        for (auto it = args.begin(); it != args.end(); ++it)
        {
            if (std::find(allowed.begin(), allowed.end(), std::string_view(it.key())) == allowed.end())
            {
                throw ArgError("unknown argument '" + it.key() + "' for " + std::string(toolName) +
                               "; valid arguments: " + detail::joined(allowed));
            }
        }
    }

    void rejectUnknownKeys(const nlohmann::json& value, std::initializer_list<std::string_view> allowed,
                           std::string_view path)
    {
        if (!value.is_object())
        {
            throw ArgError("'" + std::string(path) + "' must be an object (got " + detail::valueText(value) + ")");
        }
        for (auto it = value.begin(); it != value.end(); ++it)
        {
            if (std::find(allowed.begin(), allowed.end(), std::string_view(it.key())) == allowed.end())
            {
                throw ArgError("unknown " + std::string(path) + " key '" + it.key() + "'; valid keys: " +
                               detail::joined(allowed));
            }
        }
    }
}
