#pragma once

#include "../protocol/ArgReader.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// VK-1654 / VK-1655: strict, self-describing settings tables shared by the environment tools
// (EnvironmentToolSupport) and the render-settings tools (RenderSettingsToolSupport). A table lists
// every key a tool reports and accepts with its kind and valid range; the generic ops below report a
// struct, validate a partial update all-or-nothing, and describe the ranges and input schema. Every
// function here runs on any thread. Deliberately free of ToolHelpers.hpp (SceneTools.cpp keeps its own
// file-local entityId()).
namespace mcp::tools::env
{
    // ------------------------------------------------------------------
    // Field tables
    // ------------------------------------------------------------------

    enum class FieldKind
    {
        Bool,
        Float,
        Int,
        UInt,
        Vec3,  // glm::vec3 or float[3]; min / max bound every component
        Enum,
        String
    };

    template <typename T>
    struct FieldDesc
    {
        std::string_view key;                         // the scene serializer's JSON key (= the member name)
        FieldKind kind = FieldKind::Float;
        double min = 0.0;                             // inclusive (Float / Int / UInt / Vec3)
        double max = 0.0;
        std::span<const std::string_view> enumNames;  // Enum: canonical names, index = enumerator value
        bool weatherDriven = false;                   // weather overrides the live value while it is enabled
        std::string_view setHint;                     // non-empty: read-only in a patch; says what to use instead
        bool persisted = true;                        // false: live-only state (GI debug views), never reported
        std::string_view help;                        // units / meaning, for the input schemas
        std::string_view pathExtension;               // String: a file path with this extension ("" = free text)
        // The value in the JSON form the tools report (floats as floatJson, enums by name) ...
        nlohmann::json (*get)(const T&) = nullptr;
        // ... and the writer for a value in that same form (strictPatch validates and canonicalises first).
        void (*set)(T&, const nlohmann::json&) = nullptr;
    };

    template <typename T>
    struct SectionTable
    {
        std::string_view name;                          // "atmosphere" | "clouds" | "volumetric fog" | "GI" | "bloom" | ...
        std::vector<FieldDesc<T>> fields;               // struct order
        std::string (*crossCheck)(const T&) = nullptr;  // a staged value's cross-field error, or ""
        // Fills fields the patch implies but did not name (SSAO: a quality change sets kernelSize). Runs after
        // every key is applied to `staged` (a copy of `base`) and before crossCheck.
        void (*derive)(const T& base, T& staged, const nlohmann::json& patch) = nullptr;
    };

    // Canonical enum spellings for enumField, indexed by the enumerator's value and matched
    // case-insensitively. Specialise next to the enum's table:
    //   template <> struct EnumNames<E> { static constexpr std::span<const std::string_view> names{array}; };
    template <typename E>
    struct EnumNames;

    // ------------------------------------------------------------------
    // Values and arguments
    // ------------------------------------------------------------------

    // The shortest decimal that round-trips `value` (std::to_chars), as a JSON number: 0.05f reports
    // as 0.05, and 0.05 narrows back to exactly 0.05f. Non-finite -> null.
    nlohmann::json floatJson(float value);
    nlohmann::json vec3Json(const glm::vec3& value);

    // The checks strictPatch applies, for arguments outside a table. `path` names the value in errors.
    float requireFloat(const nlohmann::json& value, double min, double max, std::string_view path);
    glm::vec3 requireVec3(const nlohmann::json& value, double min, double max, std::string_view path);
    bool requireBool(const nlohmann::json& value, std::string_view path);
    std::string requireString(const nlohmann::json& value, std::string_view path);

    // ArgError for any top-level argument not in `allowed` ("unknown argument 'x' for <tool>; valid
    // arguments: ..."). Null arguments count as none.
    void rejectUnknownArgs(const nlohmann::json& args, std::initializer_list<std::string_view> allowed,
                           std::string_view toolName);

    // ArgError for any key of the object `value` not in `allowed`; `path` names the object.
    void rejectUnknownKeys(const nlohmann::json& value, std::initializer_list<std::string_view> allowed,
                           std::string_view path);

    // ------------------------------------------------------------------
    // Internals of the generic ops (validation and reporting). Kept out of `env` itself so that
    // argument-dependent lookup on a SectionTable never finds them.
    // ------------------------------------------------------------------

    namespace detail
    {
        // JSON spelling of the shortest round-trip float: 1e-4 stays "0.0001" rather than
        // std::to_string's "0.000100".
        std::string floatText(float value);

        // Agent input echoed in an error; replace: it may hold invalid UTF-8.
        std::string valueText(const nlohmann::json& value);

        std::string pathOf(std::string_view prefix, std::string_view key);

        bool equalsIgnoreCase(std::string_view a, std::string_view b);

        template <typename Range>
        std::string joined(const Range& names)
        {
            std::string out;
            for (const auto& name : names)
            {
                out += out.empty() ? "" : ", ";
                out += name;
            }
            return out.empty() ? std::string("none") : out;
        }

        // The bounds as the floats a value is compared against (requireFloat narrows first), so
        // moonAzimuth's max reads 359.99997 rather than its double expansion.
        std::string rangeError(std::string_view path, double min, double max, const nlohmann::json& value);

        // Every check throws ArgError naming `path`.
        int64_t checkedInteger(const nlohmann::json& value, double min, double max, std::string_view path);
        std::string_view checkedEnum(const nlohmann::json& value, std::span<const std::string_view> names,
                                     std::string_view path);

        // The value in the form FieldDesc::set takes (the form FieldDesc::get reports).
        nlohmann::json canonicalValue(FieldKind kind, double min, double max, std::span<const std::string_view> names,
                                      const nlohmann::json& value, std::string_view path);

        nlohmann::json namesJson(std::span<const std::string_view> names);
        nlohmann::json rangeEntry(FieldKind kind, double min, double max, std::span<const std::string_view> names);
        nlohmann::json fieldSchema(FieldKind kind, double min, double max, std::span<const std::string_view> names,
                                   std::string description);

        template <typename T>
        const FieldDesc<T>* findField(const SectionTable<T>& table, std::string_view key)
        {
            for (const FieldDesc<T>& field : table.fields)
            {
                if (field.key == key)
                {
                    return &field;
                }
            }
            return nullptr;
        }

        // The keys a patch may set.
        template <typename T>
        std::string settableKeys(const SectionTable<T>& table)
        {
            std::vector<std::string_view> keys;
            for (const FieldDesc<T>& field : table.fields)
            {
                if (field.persisted && field.setHint.empty())
                {
                    keys.push_back(field.key);
                }
            }
            return joined(keys);
        }

        // True when `value` names exactly what `staged` already holds for `field`, compared in the
        // reported form: the value as given, or canonicalised and written to a probe copy (so 12 and
        // 12.0, or "medium" and "Medium", match). An invalid value never matches.
        template <typename T>
        bool holdsValue(const FieldDesc<T>& field, const T& staged, const nlohmann::json& value, std::string_view path)
        {
            const nlohmann::json current = field.get(staged);
            if (value == current)
            {
                return true;
            }
            T probe = staged;
            try
            {
                field.set(probe, canonicalValue(field.kind, field.min, field.max, field.enumNames, value, path));
            }
            catch (const ArgError&)
            {
                return false;
            }
            return field.get(probe) == current;
        }
    }

    // ------------------------------------------------------------------
    // Field builders: captureless, so get / set decay to plain function pointers
    // ------------------------------------------------------------------

    template <typename T, bool T::*Member>
    FieldDesc<T> boolField(std::string_view key, std::string_view help)
    {
        FieldDesc<T> field;
        field.key = key;
        field.kind = FieldKind::Bool;
        field.help = help;
        field.get = [](const T& value) -> nlohmann::json { return value.*Member; };
        field.set = [](T& value, const nlohmann::json& v) { value.*Member = v.get<bool>(); };
        return field;
    }

    template <typename T, float T::*Member>
    FieldDesc<T> floatField(std::string_view key, double min, double max, std::string_view help, bool weather = false)
    {
        FieldDesc<T> field;
        field.key = key;
        field.kind = FieldKind::Float;
        field.min = min;
        field.max = max;
        field.weatherDriven = weather;
        field.help = help;
        field.get = [](const T& value) -> nlohmann::json { return floatJson(value.*Member); };
        field.set = [](T& value, const nlohmann::json& v) { value.*Member = static_cast<float>(v.get<double>()); };
        return field;
    }

    template <typename T, int T::*Member>
    FieldDesc<T> intField(std::string_view key, double min, double max, std::string_view help)
    {
        FieldDesc<T> field;
        field.key = key;
        field.kind = FieldKind::Int;
        field.min = min;
        field.max = max;
        field.help = help;
        field.get = [](const T& value) -> nlohmann::json { return static_cast<int64_t>(value.*Member); };
        field.set = [](T& value, const nlohmann::json& v) { value.*Member = static_cast<int>(v.get<int64_t>()); };
        return field;
    }

    template <typename T, uint32_t T::*Member>
    FieldDesc<T> uintField(std::string_view key, double min, double max, std::string_view help)
    {
        FieldDesc<T> field;
        field.key = key;
        field.kind = FieldKind::UInt;
        field.min = min;
        field.max = max;
        field.help = help;
        field.get = [](const T& value) -> nlohmann::json { return static_cast<int64_t>(value.*Member); };
        field.set = [](T& value, const nlohmann::json& v) { value.*Member = static_cast<uint32_t>(v.get<int64_t>()); };
        return field;
    }

    template <typename T, glm::vec3 T::*Member>
    FieldDesc<T> vec3Field(std::string_view key, double min, double max, std::string_view help, bool weather = false)
    {
        FieldDesc<T> field;
        field.key = key;
        field.kind = FieldKind::Vec3;
        field.min = min;
        field.max = max;
        field.weatherDriven = weather;
        field.help = help;
        field.get = [](const T& value) -> nlohmann::json { return vec3Json(value.*Member); };
        field.set = [](T& value, const nlohmann::json& v)
        {
            value.*Member = glm::vec3(static_cast<float>(v.at(0).get<double>()), static_cast<float>(v.at(1).get<double>()),
                                      static_cast<float>(v.at(2).get<double>()));
        };
        return field;
    }

    // A float[3] member (VolumetricFogSettings::fogColor).
    template <typename T, float (T::*Member)[3]>
    FieldDesc<T> rgbArrayField(std::string_view key, double min, double max, std::string_view help, bool weather = false)
    {
        FieldDesc<T> field;
        field.key = key;
        field.kind = FieldKind::Vec3;
        field.min = min;
        field.max = max;
        field.weatherDriven = weather;
        field.help = help;
        field.get = [](const T& value) -> nlohmann::json
        {
            const float (&rgb)[3] = value.*Member;
            return nlohmann::json::array({floatJson(rgb[0]), floatJson(rgb[1]), floatJson(rgb[2])});
        };
        field.set = [](T& value, const nlohmann::json& v)
        {
            float (&rgb)[3] = value.*Member;
            for (std::size_t i = 0; i < 3; ++i)
            {
                rgb[i] = static_cast<float>(v.at(i).get<double>());
            }
        };
        return field;
    }

    template <typename T, typename E, E T::*Member>
    FieldDesc<T> enumField(std::string_view key, std::string_view help)
    {
        FieldDesc<T> field;
        field.key = key;
        field.kind = FieldKind::Enum;
        field.enumNames = EnumNames<E>::names;
        field.help = help;
        field.get = [](const T& value) -> nlohmann::json
        {
            const std::span<const std::string_view> names = EnumNames<E>::names;
            const auto index = static_cast<std::size_t>(value.*Member);
            return index < names.size() ? nlohmann::json(std::string(names[index])) : nlohmann::json(index);
        };
        field.set = [](T& value, const nlohmann::json& v)
        {
            const std::span<const std::string_view> names = EnumNames<E>::names;
            const std::string name = v.get<std::string>();
            for (std::size_t i = 0; i < names.size(); ++i)
            {
                if (names[i] == name)
                {
                    value.*Member = static_cast<E>(i);
                    return;
                }
            }
            throw std::logic_error("'" + name + "' is not a canonical enum name");
        };
        return field;
    }

    // A std::string member. With an `extension` it is a file path: the tool resolves and checks the path
    // before strictPatch sees it (the table itself only requires a string).
    template <typename T, std::string T::*Member>
    FieldDesc<T> stringField(std::string_view key, std::string_view extension, std::string_view help)
    {
        FieldDesc<T> field;
        field.key = key;
        field.kind = FieldKind::String;
        field.pathExtension = extension;
        field.help = help;
        field.get = [](const T& value) -> nlohmann::json { return value.*Member; };
        field.set = [](T& value, const nlohmann::json& v) { value.*Member = v.get<std::string>(); };
        return field;
    }

    // A key reported by *_get that a patch may not set; `hint` names what sets it instead.
    template <typename T>
    FieldDesc<T> readOnly(FieldDesc<T> field, std::string_view hint)
    {
        field.setHint = hint;
        return field;
    }

    // Live-only state: never part of the section's JSON, refused in a patch with `hint`.
    template <typename T>
    FieldDesc<T> liveOnly(FieldDesc<T> field, std::string_view hint)
    {
        field.setHint = hint;
        field.persisted = false;
        return field;
    }

    // ------------------------------------------------------------------
    // Generic ops over a table
    // ------------------------------------------------------------------

    // Every persisted key -> its reported value.
    template <typename T>
    nlohmann::json toJson(const SectionTable<T>& table, const T& value)
    {
        nlohmann::json out = nlohmann::json::object();
        for (const FieldDesc<T>& field : table.fields)
        {
            if (field.persisted)
            {
                out[std::string(field.key)] = field.get(value);
            }
        }
        return out;
    }

    // `base` with the keys of `patch` applied, all-or-nothing. `patch` must be an object; an unknown
    // key, a wrong type, an out-of-range value or a read-only key (setHint) set to anything but its
    // current value throws ArgError naming '<prefix>.<key>' (just '<key>' when `prefix` is empty, i.e.
    // the patch is a tool's own arguments). A read-only key echoing its current value is skipped, so a
    // whole *_get object can be passed back.
    // Numbers are checked as JSON numbers (booleans are not numbers), must be finite and fit a float,
    // and are narrowed to float BEFORE the inclusive [min, max] test against the float bounds, so a
    // value a *_get reported is always accepted back. Ends with the table's derive, then its crossCheck.
    template <typename T>
    T strictPatch(const SectionTable<T>& table, const T& base, const nlohmann::json& patch, std::string_view prefix)
    {
        if (!patch.is_object())
        {
            throw ArgError(prefix.empty() ? std::string("the arguments must be an object")
                                          : "'" + std::string(prefix) + "' must be an object (got " +
                                                detail::valueText(patch) + ")");
        }

        T staged = base;
        for (auto it = patch.begin(); it != patch.end(); ++it)
        {
            const FieldDesc<T>* field = detail::findField(table, it.key());
            if (field == nullptr)
            {
                throw ArgError(prefix.empty()
                                   ? "unknown argument '" + it.key() + "'; valid arguments: " + detail::settableKeys(table)
                                   : "unknown " + std::string(prefix) + " key '" + it.key() + "'; valid keys: " +
                                         detail::settableKeys(table));
            }
            const std::string path = detail::pathOf(prefix, field->key);
            if (!field->setHint.empty())
            {
                // Echoing the current value back (e.g. a whole environment_get object) is a no-op. Live-only
                // keys (the GI debug views) are never reported, so they are always refused with the hint.
                if (field->persisted && detail::holdsValue(*field, staged, it.value(), path))
                {
                    continue;
                }
                throw ArgError("'" + path + "' is read-only here: " + std::string(field->setHint));
            }
            field->set(staged, detail::canonicalValue(field->kind, field->min, field->max, field->enumNames, it.value(),
                                                      path));
        }

        if (table.derive != nullptr)
        {
            table.derive(base, staged, patch);
        }
        if (table.crossCheck != nullptr)
        {
            const std::string error = table.crossCheck(staged);
            if (!error.empty())
            {
                throw ArgError(prefix.empty() ? error : std::string(prefix) + ": " + error);
            }
        }
        return staged;
    }

    // {key: {type, min, max | values, weatherDriven?, readOnly?, extension?}} for every persisted key.
    template <typename T>
    nlohmann::json rangesJson(const SectionTable<T>& table)
    {
        nlohmann::json out = nlohmann::json::object();
        for (const FieldDesc<T>& field : table.fields)
        {
            if (!field.persisted)
            {
                continue;
            }
            nlohmann::json entry = detail::rangeEntry(field.kind, field.min, field.max, field.enumNames);
            if (field.weatherDriven)
            {
                entry["weatherDriven"] = true;
            }
            if (!field.setHint.empty())
            {
                entry["readOnly"] = std::string(field.setHint);
            }
            if (!field.pathExtension.empty())
            {
                entry["extension"] = std::string(field.pathExtension);
            }
            out[std::string(field.key)] = std::move(entry);
        }
        return out;
    }

    // A JSON-Schema object of the settable keys (additionalProperties: false).
    template <typename T>
    nlohmann::json schemaJson(const SectionTable<T>& table, std::string description)
    {
        nlohmann::json properties = nlohmann::json::object();
        for (const FieldDesc<T>& field : table.fields)
        {
            if (!field.persisted || !field.setHint.empty())
            {
                continue;
            }
            std::string help(field.help);
            if (field.weatherDriven)
            {
                help += " Overridden live while weather is enabled.";
            }
            properties[std::string(field.key)] =
                detail::fieldSchema(field.kind, field.min, field.max, field.enumNames, std::move(help));
        }
        nlohmann::json out = schema::object(std::move(properties));
        out["description"] = std::move(description);
        out["additionalProperties"] = false;
        return out;
    }

    // The persisted keys whose reported value differs between `from` and `to`, with `to`'s value.
    template <typename T>
    nlohmann::json diffJson(const SectionTable<T>& table, const T& from, const T& to)
    {
        nlohmann::json out = nlohmann::json::object();
        for (const FieldDesc<T>& field : table.fields)
        {
            if (!field.persisted)
            {
                continue;
            }
            nlohmann::json after = field.get(to);
            if (field.get(from) != after)
            {
                out[std::string(field.key)] = std::move(after);
            }
        }
        return out;
    }

    // The keys weather drives live, in table order.
    template <typename T>
    std::vector<std::string> weatherDrivenKeys(const SectionTable<T>& table)
    {
        std::vector<std::string> keys;
        for (const FieldDesc<T>& field : table.fields)
        {
            if (field.weatherDriven)
            {
                keys.emplace_back(field.key);
            }
        }
        return keys;
    }
}
