#include "VFXToolSupport.hpp"
#include "PathSandbox.hpp"
#include "SettingsTable.hpp"
#include "TerrainToolSupport.hpp"

#include "vfx/VFXBlendMode.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <variant>

namespace mcp::tools::vfxtool
{
    namespace
    {
        namespace fs = std::filesystem;

        constexpr std::string_view comboExtension = ".vfVFXSequence";

        const vfx::VFXParamOverride* findOverride(const std::vector<vfx::VFXParamOverride>& overrides,
                                                  std::string_view name)
        {
            const auto it = std::find_if(overrides.begin(), overrides.end(),
                                         [name](const vfx::VFXParamOverride& entry) { return entry.name == name; });
            return it != overrides.end() ? &*it : nullptr;
        }

        template <typename T>
        std::optional<T> overrideOf(const std::vector<vfx::VFXParamOverride>& overrides, std::string_view name)
        {
            const vfx::VFXParamOverride* found = findOverride(overrides, name);
            if (found == nullptr)
            {
                return std::nullopt;
            }
            if (const T* value = std::get_if<T>(&found->value))
            {
                return *value;
            }
            return std::nullopt;
        }

        nlohmann::json vec4Json(const glm::vec4& value)
        {
            return nlohmann::json::array({env::floatJson(value.x), env::floatJson(value.y), env::floatJson(value.z),
                                          env::floatJson(value.w)});
        }

        // Help-text spelling: 0.01 stays "0.01", 1000.0 reads "1000".
        std::string numberText(double value)
        {
            return std::format("{}", value);
        }

        const char* typeLabel(vfx::VFXPropertyType type)
        {
            switch (type)
            {
            case vfx::VFXPropertyType::Float: return "number";
            case vfx::VFXPropertyType::Int:   return "integer";
            case vfx::VFXPropertyType::Bool:  return "boolean";
            case vfx::VFXPropertyType::Vec3:  return "[x, y, z]";
            case vfx::VFXPropertyType::Color: return "[r, g, b, a]";
            default:                          return "value";
            }
        }

        // A finite number that fits a float. Checked as a double first: 1e39 is a finite double but an
        // infinite float.
        float finiteFloat(const nlohmann::json& value, std::string_view path)
        {
            if (!value.is_number())
            {
                throw ArgError("'" + std::string(path) + "' must be a number (got " + env::detail::valueText(value) + ")");
            }
            const double number = value.get<double>();
            if (!std::isfinite(number) || std::fabs(number) > static_cast<double>(std::numeric_limits<float>::max()))
            {
                throw ArgError("'" + std::string(path) + "' must be a finite number (got " + env::detail::valueText(value) +
                               ")");
            }
            return static_cast<float>(number);
        }

        glm::vec3 finiteVec3(const nlohmann::json& value, std::string_view path)
        {
            if (!value.is_array() || value.size() != 3)
            {
                throw ArgError("'" + std::string(path) + "' must be [x, y, z] (got " + env::detail::valueText(value) + ")");
            }
            glm::vec3 out(0.0f);
            for (std::size_t i = 0; i < 3; ++i)
            {
                out[static_cast<glm::length_t>(i)] = finiteFloat(value[i], std::string(path) + "[" + std::to_string(i) + "]");
            }
            return out;
        }

        // [r, g, b] (alpha 1) or [r, g, b, a]; `component(value, index, path)` checks and narrows each number.
        template <typename CheckComponent>
        glm::vec4 colorFrom(const nlohmann::json& value, std::string_view path, CheckComponent component)
        {
            if (!value.is_array() || (value.size() != 3 && value.size() != 4))
            {
                throw ArgError("'" + std::string(path) + "' must be [r, g, b] or [r, g, b, a] (got " +
                               env::detail::valueText(value) + ")");
            }
            glm::vec4 out(1.0f);
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                out[static_cast<glm::length_t>(i)] =
                    component(value[i], i, std::string(path) + "[" + std::to_string(i) + "]");
            }
            return out;
        }

        // A table value under ValueRules::Stored: typed by the parameter's registry entry and checked with
        // vfx::isValidOverrideValue only (a blend mode still by name), so a value outside the tool's range replays.
        vfx::VFXPropertyValue parseStoredParamValue(const ParamSpec& spec, const nlohmann::json& value,
                                                    std::string_view path)
        {
            if (spec.kind == ParamKind::BlendMode && value.is_string())
            {
                return parseParamValue(spec, value, path);
            }
            const vfx::VFXExposedParameter* parameter = vfx::findExposedParameter(spec.overrideName);
            if (parameter == nullptr)
            {
                throw std::logic_error("VFX parameter '" + std::string(spec.overrideName) + "' is not exposed");
            }
            return parseOtherValue(*parameter, value, path);
        }

        // "otherParams": `others` becomes the whole set of overrides outside the table. Table overrides keep their
        // place, the others are updated in place or dropped, new ones are appended in key order.
        std::vector<vfx::VFXParamOverride> replaceOtherOverrides(const std::vector<vfx::VFXParamOverride>& overrides,
                                                                 const nlohmann::json& others, const std::string& path)
        {
            if (!others.is_null() && !others.is_object())
            {
                throw ArgError("'" + path + "' must be an object of parameter name -> value (got " +
                               env::detail::valueText(others) + ")");
            }

            std::vector<vfx::VFXParamOverride> wanted;
            if (others.is_object())
            {
                for (auto it = others.begin(); it != others.end(); ++it)
                {
                    const std::string entryPath = env::detail::pathOf(path, it.key());
                    if (const ParamSpec* spec = findParamByOverride(it.key()))
                    {
                        throw ArgError("'" + entryPath + "' is not an " + std::string(otherParamsKey) + " key: set it as '" +
                                       std::string(spec->key) + "'");
                    }
                    const vfx::VFXExposedParameter* parameter = vfx::findExposedParameter(it.key());
                    if (parameter == nullptr)
                    {
                        throw ArgError("unknown " + path + " key '" + it.key() + "'; valid keys: " +
                                       env::detail::joined(otherParamNames()));
                    }
                    if (it->is_null())
                    {
                        continue; // no override, like an absent key
                    }
                    const vfx::VFXParamOverride* existing = findOverride(overrides, it.key());
                    if (existing != nullptr && *it == otherValueJson(existing->value))
                    {
                        wanted.push_back(*existing);
                        continue;
                    }
                    wanted.push_back({it.key(), parseOtherValue(*parameter, *it, entryPath)});
                }
            }

            std::vector<vfx::VFXParamOverride> merged;
            std::vector<bool> placed(wanted.size(), false);
            for (const vfx::VFXParamOverride& entry : overrides)
            {
                if (findParamByOverride(entry.name) != nullptr)
                {
                    merged.push_back(entry);
                    continue;
                }
                for (std::size_t i = 0; i < wanted.size(); ++i)
                {
                    if (!placed[i] && wanted[i].name == entry.name)
                    {
                        merged.push_back(wanted[i]);
                        placed[i] = true;
                        break;
                    }
                }
            }
            for (std::size_t i = 0; i < wanted.size(); ++i)
            {
                if (!placed[i])
                {
                    merged.push_back(std::move(wanted[i]));
                }
            }
            return merged;
        }
    }

    // ------------------------------------------------------------------
    // Keys and values
    // ------------------------------------------------------------------

    const ParamSpec* findParam(std::string_view key)
    {
        for (const ParamSpec& spec : paramSpecs)
        {
            if (spec.key == key)
            {
                return &spec;
            }
        }
        return nullptr;
    }

    const ParamSpec* findParamByOverride(std::string_view overrideName)
    {
        for (const ParamSpec& spec : paramSpecs)
        {
            if (spec.overrideName == overrideName)
            {
                return &spec;
            }
        }
        return nullptr;
    }

    std::vector<std::string_view> otherParamNames()
    {
        std::vector<std::string_view> names;
        for (const vfx::VFXExposedParameter& parameter : vfx::kExposedParameters)
        {
            if (findParamByOverride(parameter.name) == nullptr)
            {
                names.push_back(parameter.name);
            }
        }
        return names;
    }

    std::string paramKeyList(bool withOtherParams)
    {
        std::string keys;
        for (const ParamSpec& spec : paramSpecs)
        {
            keys += keys.empty() ? "" : ", ";
            keys += spec.key;
        }
        if (withOtherParams)
        {
            keys += ", ";
            keys += otherParamsKey;
        }
        return keys;
    }

    std::string paramsHelp(bool withOtherParams)
    {
        std::string text;
        for (const ParamSpec& spec : paramSpecs)
        {
            text += text.empty() ? "" : ", ";
            text += std::string(spec.key) + " (";
            switch (spec.kind)
            {
            case ParamKind::Float:
            case ParamKind::Int:
                text += numberText(spec.min) + " to " + numberText(spec.max);
                break;
            case ParamKind::Tint:
                text += "[r, g, b] or [r, g, b, a], RGB " + numberText(spec.min) + " to " + numberText(spec.max) +
                        ", alpha 0 to " + numberText(maxTintAlpha);
                break;
            case ParamKind::BlendMode:
                for (std::size_t i = 0; i < blendModeNames.size(); ++i)
                {
                    text += i == 0 ? "" : " | ";
                    text += blendModeNames[i];
                }
                break;
            }
            text += ": " + std::string(spec.help) + ")";
        }
        if (!withOtherParams)
        {
            return text;
        }

        // The other names, grouped by type in registry order.
        std::string others;
        const char* group = nullptr;
        for (const vfx::VFXExposedParameter& parameter : vfx::kExposedParameters)
        {
            if (findParamByOverride(parameter.name) != nullptr)
            {
                continue;
            }
            const char* label = typeLabel(parameter.type);
            if (group != nullptr && std::string_view(group) != label)
            {
                others += std::string(" (") + group + "); ";
            }
            else if (group != nullptr)
            {
                others += ", ";
            }
            others += parameter.name;
            group = label;
        }
        if (group != nullptr)
        {
            others += std::string(" (") + group + ")";
        }
        text += ", " + std::string(otherParamsKey) + " ({name: value}, replacing every other override as a whole: " +
                others + ")";
        return text;
    }

    bool isComboPath(std::string_view path)
    {
        const fs::path file = pathFromUtf8(path);
        return file.has_filename() && detail::componentEquals(file.extension(), pathFromUtf8(comboExtension));
    }

    vfx::VFXPropertyValue parseParamValue(const ParamSpec& spec, const nlohmann::json& value, std::string_view path)
    {
        switch (spec.kind)
        {
        case ParamKind::Float:
            return vfx::VFXPropertyValue(std::in_place_type<float>, env::requireFloat(value, spec.min, spec.max, path));
        case ParamKind::Tint:
        {
            const double minimum = spec.min;
            const double maximum = spec.max;
            return vfx::VFXPropertyValue(std::in_place_type<glm::vec4>, colorFrom(value, path,
                [minimum, maximum](const nlohmann::json& component, std::size_t index, const std::string& componentPath)
                {
                    return env::requireFloat(component, minimum, index < 3 ? maximum : maxTintAlpha, componentPath);
                }));
        }
        case ParamKind::BlendMode:
        {
            const std::string_view name = env::detail::checkedEnum(value, blendModeNames, path);
            const auto index = std::find(blendModeNames.begin(), blendModeNames.end(), name) - blendModeNames.begin();
            return vfx::VFXPropertyValue(std::in_place_type<int32_t>, static_cast<int32_t>(index));
        }
        case ParamKind::Int:
            return vfx::VFXPropertyValue(std::in_place_type<int32_t>,
                                         static_cast<int32_t>(env::detail::checkedInteger(value, spec.min, spec.max, path)));
        }
        throw std::logic_error("unknown VFX parameter kind");
    }

    nlohmann::json paramValueJson(const ParamSpec& spec, const vfx::VFXPropertyValue& value)
    {
        switch (spec.kind)
        {
        case ParamKind::Float:
            if (const float* number = std::get_if<float>(&value))
            {
                return env::floatJson(*number);
            }
            break;
        case ParamKind::Tint:
            if (const glm::vec4* tint = std::get_if<glm::vec4>(&value))
            {
                return vec4Json(*tint);
            }
            break;
        case ParamKind::BlendMode:
            if (const int32_t* mode = std::get_if<int32_t>(&value))
            {
                if (*mode >= 0 && static_cast<std::size_t>(*mode) < blendModeNames.size())
                {
                    return std::string(blendModeNames[static_cast<std::size_t>(*mode)]);
                }
                return *mode;
            }
            break;
        case ParamKind::Int:
            if (const int32_t* number = std::get_if<int32_t>(&value))
            {
                return *number;
            }
            break;
        }
        return nullptr;
    }

    vfx::VFXPropertyValue parseOtherValue(const vfx::VFXExposedParameter& parameter, const nlohmann::json& value,
                                          std::string_view path)
    {
        vfx::VFXPropertyValue parsed;
        switch (parameter.type)
        {
        case vfx::VFXPropertyType::Float:
            parsed.emplace<float>(finiteFloat(value, path));
            break;
        case vfx::VFXPropertyType::Int:
            parsed.emplace<int32_t>(static_cast<int32_t>(env::detail::checkedInteger(
                value, static_cast<double>(std::numeric_limits<int32_t>::min()),
                static_cast<double>(std::numeric_limits<int32_t>::max()), path)));
            break;
        case vfx::VFXPropertyType::Bool:
            parsed.emplace<bool>(env::requireBool(value, path));
            break;
        case vfx::VFXPropertyType::Vec3:
            parsed.emplace<glm::vec3>(finiteVec3(value, path));
            break;
        case vfx::VFXPropertyType::Color:
            parsed.emplace<glm::vec4>(colorFrom(value, path,
                [](const nlohmann::json& component, std::size_t, const std::string& componentPath)
                {
                    return finiteFloat(component, componentPath);
                }));
            break;
        default:
            throw std::logic_error("parameter '" + std::string(parameter.name) + "' has a type no override takes");
        }
        if (!vfx::isValidOverrideValue(parameter, parsed))
        {
            throw ArgError("'" + std::string(path) + "' is not a valid " + std::string(parameter.name) + " value (got " +
                           env::detail::valueText(value) + ")");
        }
        return parsed;
    }

    nlohmann::json otherValueJson(const vfx::VFXPropertyValue& value)
    {
        if (const float* number = std::get_if<float>(&value))
        {
            return env::floatJson(*number);
        }
        if (const int32_t* integer = std::get_if<int32_t>(&value))
        {
            return *integer;
        }
        if (const bool* flag = std::get_if<bool>(&value))
        {
            return *flag;
        }
        if (const glm::vec3* vector = std::get_if<glm::vec3>(&value))
        {
            return env::vec3Json(*vector);
        }
        if (const glm::vec4* color = std::get_if<glm::vec4>(&value))
        {
            return vec4Json(*color);
        }
        return nullptr;
    }

    std::vector<vfx::VFXParamOverride> patchOverrides(const std::vector<vfx::VFXParamOverride>& current,
                                                      const nlohmann::json& patch, std::string_view prefix,
                                                      ValueRules rules)
    {
        if (!patch.is_object())
        {
            throw ArgError(prefix.empty() ? std::string("the parameters must be an object of key -> value")
                                          : "'" + std::string(prefix) + "' must be an object (got " +
                                                env::detail::valueText(patch) + ")");
        }

        std::vector<vfx::VFXParamOverride> result = current;
        for (auto it = patch.begin(); it != patch.end(); ++it)
        {
            if (it.key() == otherParamsKey)
            {
                continue;
            }
            const ParamSpec* spec = findParam(it.key());
            if (spec == nullptr)
            {
                throw ArgError("unknown " + (prefix.empty() ? std::string("parameter") : std::string(prefix) + " key") +
                               " '" + it.key() + "'; valid keys: " + paramKeyList(true));
            }
            const auto existing = std::find_if(result.begin(), result.end(), [spec](const vfx::VFXParamOverride& entry)
            {
                return entry.name == spec->overrideName;
            });
            if (it->is_null())
            {
                if (existing != result.end())
                {
                    result.erase(existing);
                }
                continue;
            }
            // The value it already holds stays as stored, even outside the tool's range (set in the inspector or
            // a scene file): undo replays a whole reported state.
            if (existing != result.end() && *it == paramValueJson(*spec, existing->value))
            {
                continue;
            }
            const std::string path = env::detail::pathOf(prefix, spec->key);
            vfx::VFXPropertyValue value = rules == ValueRules::Tool ? parseParamValue(*spec, *it, path)
                                                                    : parseStoredParamValue(*spec, *it, path);
            if (existing != result.end())
            {
                existing->value = std::move(value);
            }
            else
            {
                result.push_back({std::string(spec->overrideName), std::move(value)});
            }
        }

        if (const auto others = patch.find(otherParamsKey); others != patch.end())
        {
            result = replaceOtherOverrides(result, *others, env::detail::pathOf(prefix, otherParamsKey));
        }
        return result;
    }

    void writeParams(nlohmann::json& out, const std::vector<vfx::VFXParamOverride>& overrides)
    {
        for (const ParamSpec& spec : paramSpecs)
        {
            const vfx::VFXParamOverride* found = findOverride(overrides, spec.overrideName);
            out[std::string(spec.key)] = found != nullptr ? paramValueJson(spec, found->value) : nlohmann::json(nullptr);
        }
        nlohmann::json others = nlohmann::json::object();
        for (const vfx::VFXParamOverride& entry : overrides)
        {
            if (findParamByOverride(entry.name) == nullptr)
            {
                others[entry.name] = otherValueJson(entry.value);
            }
        }
        out[std::string(otherParamsKey)] = std::move(others);
    }

    // ------------------------------------------------------------------
    // Authored vs effective
    // ------------------------------------------------------------------

    nlohmann::json authoredParamJson(const ParamSpec& spec, const services::VFXEffectSummary* effect)
    {
        if (spec.kind == ParamKind::Tint)
        {
            return vec4Json(glm::vec4(1.0f));
        }
        if (effect == nullptr)
        {
            return nullptr;
        }
        if (spec.overrideName == "spawnRate")
        {
            return env::floatJson(effect->spawnRate);
        }
        if (spec.overrideName == "lifetime")
        {
            return env::floatJson(effect->lifetime);
        }
        if (spec.overrideName == "startSize")
        {
            return env::floatJson(effect->startSize);
        }
        if (spec.kind == ParamKind::BlendMode)
        {
            return std::string(vfx::blendModeToString(effect->blendMode));
        }
        if (spec.overrideName == "sortOrder")
        {
            return effect->sortOrder;
        }
        return nullptr;
    }

    nlohmann::json paramsReportJson(const services::VFXEffectSummary* effect,
                                    const std::vector<vfx::VFXParamOverride>& overrides)
    {
        nlohmann::json out = nlohmann::json::object();
        for (const ParamSpec& spec : paramSpecs)
        {
            const vfx::VFXParamOverride* found = findOverride(overrides, spec.overrideName);
            nlohmann::json overrideValue = found != nullptr ? paramValueJson(spec, found->value) : nlohmann::json(nullptr);
            nlohmann::json authored = authoredParamJson(spec, effect);
            nlohmann::json effective = overrideValue.is_null() ? authored : overrideValue;
            out[std::string(spec.key)] = {
                {"override", std::move(overrideValue)},
                {"authored", std::move(authored)},
                {"effective", std::move(effective)}
            };
        }
        return out;
    }

    glm::vec4 authoredBirthColor(const services::VFXEffectSummary& effect)
    {
        return effect.colorOverLifetime ? effect.birthColor : effect.startColor;
    }

    glm::vec4 effectiveBirthColor(const services::VFXEffectSummary& effect,
                                  const std::vector<vfx::VFXParamOverride>& overrides)
    {
        const glm::vec4 base = effect.colorOverLifetime
            ? effect.birthColor
            : overrideOf<glm::vec4>(overrides, "startColor").value_or(effect.startColor);
        return base * overrideOf<glm::vec4>(overrides, "colorTint").value_or(glm::vec4(1.0f));
    }

    nlohmann::json birthColorJson(const services::VFXEffectSummary* effect,
                                  const std::vector<vfx::VFXParamOverride>& overrides)
    {
        if (effect == nullptr)
        {
            return nullptr;
        }
        return {
            {"authored", vec4Json(authoredBirthColor(*effect))},
            {"effective", vec4Json(effectiveBirthColor(*effect, overrides))},
            {"source", effect->colorOverLifetime ? "colorOverLifetime" : "startColor"}
        };
    }

    float effectiveLifetime(const services::VFXEffectSummary& effect, const std::vector<vfx::VFXParamOverride>& overrides)
    {
        return overrideOf<float>(overrides, "lifetime").value_or(effect.lifetime);
    }

    // ------------------------------------------------------------------
    // Reporting
    // ------------------------------------------------------------------

    std::string projectPath(const std::optional<fs::path>& root, const std::string& enginePath)
    {
        if (enginePath.empty())
        {
            return {};
        }
        return projectRelativeUtf8(root, terrainEnginePath(root, enginePath));
    }

    std::optional<std::string> insideProjectPath(const fs::path& root, const std::string& enginePath)
    {
        if (enginePath.empty())
        {
            return std::nullopt;
        }
        std::error_code ec;
        const fs::path relative = fs::relative(terrainEnginePath(root, enginePath), root, ec);
        if (ec || relative.empty() || relative == fs::path(".") || *relative.begin() == "..")
        {
            return std::nullopt;
        }
        return genericPathToUtf8(relative);
    }

    nlohmann::json effectJson(const services::VFXAssetSummary& summary, const std::optional<fs::path>& root)
    {
        nlohmann::json out{
            {"path", projectPath(root, summary.path)},
            {"name", summary.name},
            {"valid", summary.valid && summary.effect.has_value()}
        };
        if (!summary.valid || !summary.effect.has_value())
        {
            out["error"] = summary.error.empty() ? std::string("not a readable .vfVFX effect") : summary.error;
            return out;
        }

        const services::VFXEffectSummary& effect = *summary.effect;
        nlohmann::json events = nlohmann::json::array();
        for (const services::VFXEventSummary& event : effect.events)
        {
            events.push_back(nlohmann::json{
                {"type", event.type},
                {"notify", event.notify},
                {"subEffect", event.subEffect.empty() ? nlohmann::json(nullptr) : nlohmann::json(projectPath(root, event.subEffect))}
            });
        }
        out["spawnRate"] = env::floatJson(effect.spawnRate);
        out["lifetime"] = env::floatJson(effect.lifetime);
        out["size"] = env::floatJson(effect.startSize);
        out["startSpeed"] = env::floatJson(effect.startSpeed);
        out["startColor"] = vec4Json(effect.startColor);
        out["looping"] = effect.looping;
        out["burstCount"] = effect.burstCount;
        out["colorOverLifetime"] = effect.colorOverLifetime;
        out["birthColor"] = vec4Json(effect.birthColor);
        out["deathColor"] = vec4Json(effect.deathColor);
        out["blendMode"] = vfx::blendModeToString(effect.blendMode);
        out["sortOrder"] = effect.sortOrder;
        out["renderMode"] = services::vfxRenderModeName(effect.renderMode);
        out["emitsLight"] = effect.emitsLight;
        out["texture"] = projectPath(root, effect.texture);
        out["events"] = std::move(events);
        return out;
    }

    nlohmann::json comboJson(const services::VFXAssetSummary& summary, const std::optional<fs::path>& root)
    {
        nlohmann::json out{
            {"path", projectPath(root, summary.path)},
            {"name", summary.name},
            {"valid", summary.valid && summary.combo.has_value()}
        };
        if (!summary.valid || !summary.combo.has_value())
        {
            out["error"] = summary.error.empty() ? std::string("not a readable .vfVFXSequence combo") : summary.error;
            return out;
        }

        const services::VFXComboSummary& combo = *summary.combo;
        nlohmann::json steps = nlohmann::json::array();
        for (const services::VFXComboStepSummary& step : combo.steps)
        {
            steps.push_back(nlohmann::json{
                {"label", step.label},
                {"kind", step.kind},
                {"effect", step.effectPath.empty() ? nlohmann::json(nullptr) : nlohmann::json(projectPath(root, step.effectPath))},
                {"startTime", env::floatJson(step.startTime)},
                {"cue", step.cue.empty() ? nlohmann::json(nullptr) : nlohmann::json(step.cue)},
                {"loop", step.loop}
            });
        }
        out["steps"] = std::move(steps);
        out["cues"] = combo.cues;
        out["playbackRate"] = env::floatJson(combo.playbackRate);
        return out;
    }
}
