#pragma once

#include "VFXParameterRegistry.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// VK-1657: the JSON codec of vfx::VFXParamOverride, shared by combo steps (.vfVFXSequence) and the
// per-entity overrides of components::VFXComponent (scene / prefab / sector JSON). One entry is
// [name, typeString, value], e.g. ["startSize", "Float", 1.6]. Header-only: Serialization.dll
// serializes the component and does not link the VFX lib.
namespace vfx
{
    namespace detail
    {
        inline std::optional<VFXPropertyType> parsePropertyTypeStrict(const std::string& value)
        {
            constexpr std::array allTypes{
                VFXPropertyType::Float,
                VFXPropertyType::Vec2,
                VFXPropertyType::Vec3,
                VFXPropertyType::Vec4,
                VFXPropertyType::Color,
                VFXPropertyType::Int,
                VFXPropertyType::Bool,
                VFXPropertyType::String,
                VFXPropertyType::Curve,
                VFXPropertyType::Gradient
            };

            for (VFXPropertyType type : allTypes)
            {
                if (value == propertyTypeToString(type))
                    return type;
            }
            return std::nullopt;
        }

        inline nlohmann::json serializePropertyValue(const VFXPropertyValue& value, VFXPropertyType type)
        {
            using json = nlohmann::json;
            switch (type)
            {
            case VFXPropertyType::Float:
                return std::holds_alternative<float>(value) ? std::get<float>(value) : 0.0f;
            case VFXPropertyType::Vec2:
                if (const auto* v = std::get_if<glm::vec2>(&value))
                    return json::array({v->x, v->y});
                return json::array({0.0f, 0.0f});
            case VFXPropertyType::Vec3:
                if (const auto* v = std::get_if<glm::vec3>(&value))
                    return json::array({v->x, v->y, v->z});
                return json::array({0.0f, 0.0f, 0.0f});
            case VFXPropertyType::Vec4:
            case VFXPropertyType::Color:
                if (const auto* v = std::get_if<glm::vec4>(&value))
                    return json::array({v->x, v->y, v->z, v->w});
                return json::array({1.0f, 1.0f, 1.0f, 1.0f});
            case VFXPropertyType::Int:
                return std::holds_alternative<int32_t>(value) ? std::get<int32_t>(value) : 0;
            case VFXPropertyType::Bool:
                return std::holds_alternative<bool>(value) ? std::get<bool>(value) : false;
            case VFXPropertyType::String:
                return std::holds_alternative<std::string>(value) ? std::get<std::string>(value) : "";
            case VFXPropertyType::Curve:
            {
                json curveJson;
                json keys = json::array();
                if (const auto* curve = std::get_if<VFXCurve>(&value))
                {
                    for (const auto& key : curve->keys)
                        keys.push_back(json::array({key.time, key.value, key.inTangent, key.outTangent}));
                }
                curveJson["keys"] = std::move(keys);
                return curveJson;
            }
            case VFXPropertyType::Gradient:
            {
                json gradientJson;
                json stops = json::array();
                if (const auto* gradient = std::get_if<VFXGradient>(&value))
                {
                    for (const auto& stop : gradient->stops)
                    {
                        stops.push_back(json::array({stop.position, stop.color.r, stop.color.g,
                                                     stop.color.b, stop.color.a}));
                    }
                }
                gradientJson["stops"] = std::move(stops);
                return gradientJson;
            }
            default:
                return 0.0f;
            }
        }

        inline VFXPropertyValue deserializePropertyValue(const nlohmann::json& j, VFXPropertyType type)
        {
            try
            {
            switch (type)
            {
            case VFXPropertyType::Float:
                return j.is_number() ? j.get<float>() : 0.0f;
            case VFXPropertyType::Vec2:
                if (j.is_array() && j.size() >= 2)
                    return glm::vec2(j[0].get<float>(), j[1].get<float>());
                return glm::vec2(0.0f);
            case VFXPropertyType::Vec3:
                if (j.is_array() && j.size() >= 3)
                    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
                return glm::vec3(0.0f);
            case VFXPropertyType::Vec4:
            case VFXPropertyType::Color:
                if (j.is_array() && j.size() >= 4)
                    return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
                return glm::vec4(1.0f);
            case VFXPropertyType::Int:
                return j.is_number_integer() ? j.get<int32_t>() : int32_t{0};
            case VFXPropertyType::Bool:
                return j.is_boolean() ? j.get<bool>() : false;
            case VFXPropertyType::String:
                return j.is_string() ? j.get<std::string>() : std::string{};
            case VFXPropertyType::Curve:
            {
                VFXCurve curve;
                if (j.is_object() && j.contains("keys") && j["keys"].is_array())
                {
                    for (const auto& key : j["keys"])
                    {
                        if (key.is_array() && key.size() >= 4)
                            curve.keys.push_back({key[0].get<float>(), key[1].get<float>(),
                                                  key[2].get<float>(), key[3].get<float>()});
                    }
                }
                return curve;
            }
            case VFXPropertyType::Gradient:
            {
                VFXGradient gradient;
                if (j.is_object() && j.contains("stops") && j["stops"].is_array())
                {
                    for (const auto& stop : j["stops"])
                    {
                        if (stop.is_array() && stop.size() >= 5)
                        {
                            gradient.stops.push_back({stop[0].get<float>(),
                                glm::vec4(stop[1].get<float>(), stop[2].get<float>(),
                                          stop[3].get<float>(), stop[4].get<float>())});
                        }
                    }
                }
                return gradient;
            }
            default:
                return 0.0f;
            }
            }
            catch (const nlohmann::json::exception&)
            {
                // Match VFXAsset::deserializePropertyValue — a single malformed value
                // defaults to 0.0f instead of failing the entire sequence load.
                return 0.0f;
            }
        }

        inline VFXPropertyType inferValueType(const VFXParamOverride& overrideValue)
        {
            if (const VFXExposedParameter* parameter = findExposedParameter(overrideValue.name))
                return parameter->type;

            const VFXPropertyValue& value = overrideValue.value;
            if (std::holds_alternative<float>(value)) return VFXPropertyType::Float;
            if (std::holds_alternative<glm::vec2>(value)) return VFXPropertyType::Vec2;
            if (std::holds_alternative<glm::vec3>(value)) return VFXPropertyType::Vec3;
            if (std::holds_alternative<glm::vec4>(value)) return VFXPropertyType::Vec4;
            if (std::holds_alternative<int32_t>(value)) return VFXPropertyType::Int;
            if (std::holds_alternative<bool>(value)) return VFXPropertyType::Bool;
            if (std::holds_alternative<std::string>(value)) return VFXPropertyType::String;
            if (std::holds_alternative<VFXCurve>(value)) return VFXPropertyType::Curve;
            if (std::holds_alternative<VFXGradient>(value)) return VFXPropertyType::Gradient;
            return VFXPropertyType::Float;
        }

        // Strict shape check for component data: the lenient deserializePropertyValue above turns a
        // malformed value into a fallback (0 / white), which must not silently become an override.
        inline bool valueJsonMatchesType(const nlohmann::json& j, VFXPropertyType type)
        {
            const auto numbers = [&j](size_t count)
            {
                if (!j.is_array() || j.size() != count)
                    return false;
                for (const auto& component : j)
                {
                    if (!component.is_number())
                        return false;
                }
                return true;
            };

            switch (type)
            {
            case VFXPropertyType::Float: return j.is_number();
            case VFXPropertyType::Int:
                // In int32 range: get<int32_t> would truncate a larger stored value into an accepted one.
                if (j.is_number_unsigned())
                    return j.get<uint64_t>() <= static_cast<uint64_t>((std::numeric_limits<int32_t>::max)());
                if (j.is_number_integer())
                {
                    const int64_t value = j.get<int64_t>();
                    return value >= (std::numeric_limits<int32_t>::min)() && value <= (std::numeric_limits<int32_t>::max)();
                }
                return false;
            case VFXPropertyType::Bool:  return j.is_boolean();
            case VFXPropertyType::Vec3:  return numbers(3);
            case VFXPropertyType::Vec4:
            case VFXPropertyType::Color: return numbers(4);
            default:                     return false; // never an exposed override type
            }
        }
    }

    inline nlohmann::json paramOverrideToJson(const VFXParamOverride& overrideValue)
    {
        const VFXPropertyType type = detail::inferValueType(overrideValue);
        return nlohmann::json::array({
            overrideValue.name,
            propertyTypeToString(type),
            detail::serializePropertyValue(overrideValue.value, type)
        });
    }

    // Any name and stored type (combo steps keep unknown names for forward compatibility of the
    // editor; the runtime ignores them).
    inline std::optional<VFXParamOverride> paramOverrideFromJson(const nlohmann::json& entry)
    {
        if (!entry.is_array() || entry.size() < 3 || !entry[0].is_string() || !entry[1].is_string())
            return std::nullopt;

        auto type = detail::parsePropertyTypeStrict(entry[1].get<std::string>());
        if (!type)
            return std::nullopt;

        VFXParamOverride overrideValue;
        overrideValue.name = entry[0].get<std::string>();
        overrideValue.value = detail::deserializePropertyValue(entry[2], *type);
        return overrideValue;
    }

    inline nlohmann::json paramOverridesToJson(const std::vector<VFXParamOverride>& overrides)
    {
        nlohmann::json array = nlohmann::json::array();
        for (const auto& overrideValue : overrides)
            array.push_back(paramOverrideToJson(overrideValue));
        return array;
    }

    // Component data (VFXComponent::overrides): only exposed names, stored with the registry's type
    // and a valid value (isValidOverride). A later entry for a name replaces the earlier one in
    // place. The name (or "<malformed>") of every dropped entry is appended to `rejected`.
    inline std::vector<VFXParamOverride> componentOverridesFromJson(const nlohmann::json& array,
                                                                    std::vector<std::string>* rejected = nullptr)
    {
        std::vector<VFXParamOverride> overrides;
        if (!array.is_array())
            return overrides;

        for (const auto& entry : array)
        {
            const bool shaped = entry.is_array() && entry.size() == 3 && entry[0].is_string() && entry[1].is_string();
            const std::string name = shaped ? entry[0].get<std::string>() : std::string("<malformed>");
            const VFXExposedParameter* parameter = shaped ? findExposedParameter(name) : nullptr;

            std::optional<VFXParamOverride> parsed;
            if (parameter && entry[1].get<std::string>() == propertyTypeToString(parameter->type) &&
                detail::valueJsonMatchesType(entry[2], parameter->type))
            {
                parsed = paramOverrideFromJson(entry);
            }
            if (!parsed || !isValidOverride(*parsed))
            {
                if (rejected)
                    rejected->push_back(name);
                continue;
            }

            auto existing = std::find_if(overrides.begin(), overrides.end(),
                [&name](const VFXParamOverride& overrideValue) { return overrideValue.name == name; });
            if (existing != overrides.end())
                existing->value = std::move(parsed->value);
            else
                overrides.push_back(std::move(*parsed));
        }
        return overrides;
    }

    // Same overrides in the same order. Compared through the JSON form because VFXCurve and
    // VFXGradient have no operator==.
    inline bool sameOverrides(const std::vector<VFXParamOverride>& a, const std::vector<VFXParamOverride>& b)
    {
        return a.size() == b.size() && paramOverridesToJson(a) == paramOverridesToJson(b);
    }
}
