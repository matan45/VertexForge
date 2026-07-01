#include "../print/Log.hpp"
#include "../uuid/UUID.hpp"
#include "VFXSequenceAsset.hpp"
#include "../resource/VFSHelpers.hpp"
#include "../serialization/AssetRefSerializationHelper.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <format>
#include <functional>

namespace vfx
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    namespace
    {
        std::function<void(const std::string&)> createWarningLogger(int& warningCount, int maxWarnings)
        {
            return [&warningCount, maxWarnings](const std::string& msg)
            {
                if (warningCount < maxWarnings)
                {
                    vfLogWarning("{}", msg);
                    warningCount++;
                    if (warningCount == maxWarnings)
                    {
                        vfLogWarning("(suppressing further warnings for this file)");
                    }
                }
            };
        }

        bool validateSequenceFileAccess(const fs::path& filePath, std::string_view path)
        {
            if (!fs::exists(filePath))
            {
                vfLogError("VFX sequence file not found: {}", path);
                return false;
            }

            std::error_code ec;
            auto fileSize = fs::file_size(filePath, ec);
            if (ec)
            {
                vfLogError("Cannot read VFX sequence file size '{}': {}", path, ec.message());
                return false;
            }

            constexpr size_t MAX_VFX_SEQUENCE_FILE_SIZE = 10 * 1024 * 1024; // 10 MB limit
            if (fileSize > MAX_VFX_SEQUENCE_FILE_SIZE)
            {
                vfLogError("VFX sequence file '{}' is too large ({} bytes, max {} bytes)",
                           path, fileSize, MAX_VFX_SEQUENCE_FILE_SIZE);
                return false;
            }

            return true;
        }

        std::optional<json> readAndParseSequenceFile(std::string_view path)
        {
            fs::path filePath(path);
            if (!validateSequenceFileAccess(filePath, path))
            {
                return std::nullopt;
            }

            json j;
            try
            {
                j = resource::readJsonFile(filePath.string());
            }
            catch (const json::parse_error& e)
            {
                vfLogError("VFX sequence file '{}' contains invalid JSON at byte {}: {}",
                           path, e.byte, e.what());
                return std::nullopt;
            }
            if (j.is_null())
            {
                vfLogError("Failed to open VFX sequence file: {}", path);
                return std::nullopt;
            }

            if (!j.is_object())
            {
                vfLogError("VFX sequence file '{}' must contain a JSON object at root level", path);
                return std::nullopt;
            }

            return j;
        }

        json vec3ToJson(const glm::vec3& v)
        {
            return json::array({v.x, v.y, v.z});
        }

        json vec4ToJson(const glm::vec4& v)
        {
            return json::array({v.x, v.y, v.z, v.w});
        }

        glm::vec3 jsonToVec3(const json& j, const glm::vec3& fallback)
        {
            if (j.is_array() && j.size() >= 3)
            {
                return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
            }
            return fallback;
        }

        glm::vec4 jsonToVec4(const json& j, const glm::vec4& fallback)
        {
            if (j.is_array() && j.size() >= 4)
            {
                return glm::vec4(j[0].get<float>(), j[1].get<float>(),
                                 j[2].get<float>(), j[3].get<float>());
            }
            return fallback;
        }

        std::optional<VFXPropertyType> parsePropertyTypeStrict(const std::string& value)
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

        json serializePropertyValue(const VFXPropertyValue& value, VFXPropertyType type)
        {
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

        VFXPropertyValue deserializePropertyValue(const json& j, VFXPropertyType type)
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

        VFXPropertyType inferValueType(const VFXParamOverride& overrideValue)
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

        json serializeParamOverride(const VFXParamOverride& overrideValue)
        {
            const VFXPropertyType type = inferValueType(overrideValue);
            return json::array({
                overrideValue.name,
                propertyTypeToString(type),
                serializePropertyValue(overrideValue.value, type)
            });
        }

        std::optional<VFXParamOverride> deserializeParamOverride(const json& entry)
        {
            if (!entry.is_array() || entry.size() < 3 || !entry[0].is_string() || !entry[1].is_string())
                return std::nullopt;

            auto type = parsePropertyTypeStrict(entry[1].get<std::string>());
            if (!type)
                return std::nullopt;

            VFXParamOverride overrideValue;
            overrideValue.name = entry[0].get<std::string>();
            overrideValue.value = deserializePropertyValue(entry[2], *type);
            return overrideValue;
        }

        VFXParamOverride migrateLegacyScalarOverride(const std::string& name, float value)
        {
            VFXParamOverride overrideValue;
            overrideValue.name = name;

            const VFXExposedParameter* parameter = findExposedParameter(name);
            if (!parameter || parameter->type == VFXPropertyType::Float)
                overrideValue.value = value;
            else if (parameter->type == VFXPropertyType::Int)
                overrideValue.value = static_cast<int32_t>(value);
            else if (parameter->type == VFXPropertyType::Bool)
                overrideValue.value = (value != 0.0f);
            else
                overrideValue.value = value;

            return overrideValue;
        }

        VFXParamOverride migrateLegacyVectorOverride(const std::string& name, const glm::vec4& value)
        {
            VFXParamOverride overrideValue;
            overrideValue.name = name;

            const VFXExposedParameter* parameter = findExposedParameter(name);
            if (parameter && parameter->type == VFXPropertyType::Vec3)
                overrideValue.value = glm::vec3(value);
            else if (parameter && parameter->type == VFXPropertyType::Color)
                overrideValue.value = value;
            else
                overrideValue.value = value;

            return overrideValue;
        }

        bool payloadEmpty(const VFXCuePayload& payload)
        {
            return !payload.position.has_value() &&
                   !payload.color.has_value() &&
                   !payload.scalar.has_value() &&
                   payload.custom.empty();
        }

        json serializePayload(const VFXCuePayload& payload)
        {
            json j;
            if (payload.position)
                j["position"] = vec3ToJson(*payload.position);
            if (payload.color)
                j["color"] = vec4ToJson(*payload.color);
            if (payload.scalar)
                j["scalar"] = *payload.scalar;
            if (!payload.custom.empty())
            {
                json custom = json::array();
                for (const auto& overrideValue : payload.custom)
                    custom.push_back(serializeParamOverride(overrideValue));
                j["custom"] = std::move(custom);
            }
            return j;
        }

        VFXCuePayload deserializePayload(const json& j)
        {
            VFXCuePayload payload;
            if (!j.is_object())
                return payload;

            if (j.contains("position"))
                payload.position = jsonToVec3(j["position"], glm::vec3(0.0f));
            if (j.contains("color"))
                payload.color = jsonToVec4(j["color"], glm::vec4(1.0f));
            if (j.contains("scalar") && j["scalar"].is_number())
                payload.scalar = j["scalar"].get<float>();
            if (j.contains("custom") && j["custom"].is_array())
            {
                for (const auto& entry : j["custom"])
                {
                    if (auto overrideValue = deserializeParamOverride(entry))
                        payload.custom.push_back(std::move(*overrideValue));
                }
            }
            return payload;
        }
    } // anonymous namespace

    json VFXSequenceAsset::serializeStep(const VFXSequenceStep& step)
    {
        json j;
        serialization::writeAssetRef(j, "vfxRef", step.vfxRef);
        j["label"] = step.label;
        j["startTime"] = step.startTime;
        j["cueName"] = step.cueName;
        j["localPosition"] = vec3ToJson(step.localPosition);
        j["localEulerDeg"] = vec3ToJson(step.localEulerDeg);
        j["localScale"] = vec3ToJson(step.localScale);
        j["loop"] = step.loop;
        j["duration"] = step.duration;
        j["stopMode"] = static_cast<int>(static_cast<uint8_t>(step.stopMode));
        j["socketName"] = step.socketName;

        json overrides = json::array();
        for (const auto& overrideValue : step.overrides)
            overrides.push_back(serializeParamOverride(overrideValue));
        j["overrides"] = overrides;

        return j;
    }

    std::optional<VFXSequenceStep> VFXSequenceAsset::deserializeStep(const json& j)
    {
        VFXSequenceStep step;

        step.vfxRef = serialization::readAssetRef(j, "vfxRef");
        if (!step.vfxRef.isValid())
        {
            return std::nullopt;
        }

        step.label = j.value("label", "");
        step.startTime = j.value("startTime", 0.0f);
        step.cueName = j.value("cueName", "");

        if (j.contains("localPosition"))
            step.localPosition = jsonToVec3(j["localPosition"], glm::vec3(0.0f));
        if (j.contains("localEulerDeg"))
            step.localEulerDeg = jsonToVec3(j["localEulerDeg"], glm::vec3(0.0f));
        if (j.contains("localScale"))
            step.localScale = jsonToVec3(j["localScale"], glm::vec3(1.0f));

        step.loop = j.value("loop", false);
        step.duration = j.value("duration", 0.0f);
        step.stopMode = static_cast<VFXStepStopMode>(
            static_cast<uint8_t>(j.value("stopMode", 0)));
        step.socketName = j.value("socketName", "");

        if (j.contains("overrides") && j["overrides"].is_array())
        {
            for (const auto& entry : j["overrides"])
            {
                if (auto overrideValue = deserializeParamOverride(entry))
                    step.overrides.push_back(std::move(*overrideValue));
            }
        }
        else if (j.contains("scalarOverrides") && j["scalarOverrides"].is_array())
        {
            for (const auto& entry : j["scalarOverrides"])
            {
                if (entry.is_array() && entry.size() >= 2 &&
                    entry[0].is_string() && entry[1].is_number())
                {
                    step.overrides.push_back(
                        migrateLegacyScalarOverride(entry[0].get<std::string>(),
                                                    entry[1].get<float>()));
                }
            }

            if (j.contains("vectorOverrides") && j["vectorOverrides"].is_array())
            {
                for (const auto& entry : j["vectorOverrides"])
                {
                    if (entry.is_array() && entry.size() >= 2 &&
                        entry[0].is_string() && entry[1].is_array() && entry[1].size() >= 4)
                    {
                        const auto& v = entry[1];
                        step.overrides.push_back(
                            migrateLegacyVectorOverride(
                                entry[0].get<std::string>(),
                                glm::vec4(v[0].get<float>(), v[1].get<float>(),
                                          v[2].get<float>(), v[3].get<float>())));
                    }
                }
            }
        }
        else if (j.contains("vectorOverrides") && j["vectorOverrides"].is_array())
        {
            for (const auto& entry : j["vectorOverrides"])
            {
                if (entry.is_array() && entry.size() >= 2 &&
                    entry[0].is_string() && entry[1].is_array() && entry[1].size() >= 4)
                {
                    const auto& v = entry[1];
                    step.overrides.push_back(
                        migrateLegacyVectorOverride(
                            entry[0].get<std::string>(),
                            glm::vec4(v[0].get<float>(), v[1].get<float>(),
                                      v[2].get<float>(), v[3].get<float>())));
                }
            }
        }

        return step;
    }

    std::optional<VFXSequenceData> VFXSequenceAsset::load(std::string_view path)
    {
        auto parsedJson = readAndParseSequenceFile(path);
        if (!parsedJson)
            return std::nullopt;

        const json& j = *parsedJson;
        VFXSequenceData data;
        int warningCount = 0;
        auto logWarning = createWarningLogger(warningCount, 20);

        try
        {
            data.version = j.value("version", VFX_SEQUENCE_FORMAT_VERSION);
            data.uuid = j.value("uuid", std::to_string(uuid::UUID().getValue()));
            data.name = j.value("name", "Unnamed Sequence");

            if (data.name.empty())
            {
                data.name = "Unnamed Sequence";
                logWarning("VFX sequence has empty name, using default");
            }

            if (j.contains("steps") && j["steps"].is_array())
            {
                const auto& stepsJson = j["steps"];
                for (size_t i = 0; i < stepsJson.size(); ++i)
                {
                    const auto& stepJson = stepsJson[i];
                    if (!stepJson.is_object())
                    {
                        logWarning(std::format("Step at index {} is not an object, skipping", i));
                        continue;
                    }
                    auto step = deserializeStep(stepJson);
                    if (!step)
                    {
                        logWarning(std::format("Step at index {} is malformed (missing/invalid vfxRef), skipping", i));
                        continue;
                    }
                    data.steps.push_back(std::move(*step));
                }
            }

            // VK-1451 timeline controls (all tolerant — absent keys keep defaults).
            data.seed = j.value("seed", 0u);
            data.playbackRate = j.value("playbackRate", 1.0f);
            data.fixedStep = j.value("fixedStep", 0.0f);
            data.prewarm = j.value("prewarm", 0.0f);

            if (j.contains("eventMarkers") && j["eventMarkers"].is_array())
            {
                for (const auto& markerJson : j["eventMarkers"])
                {
                    if (!markerJson.is_object())
                        continue;
                    VFXSequenceEventMarker marker;
                    marker.time = markerJson.value("time", 0.0f);
                    marker.cueName = markerJson.value("cueName", "");
                    if (markerJson.contains("payload"))
                        marker.payload = deserializePayload(markerJson["payload"]);
                    if (marker.cueName.empty())
                    {
                        logWarning("Event marker with empty cueName, skipping");
                        continue;
                    }
                    data.eventMarkers.push_back(std::move(marker));
                }
            }

            if (warningCount > 0)
                vfLogWarning("Loaded VFX sequence '{}' with {} warning(s)", data.name, warningCount);

            return data;
        }
        catch (const json::exception& e)
        {
            vfLogError("Failed to parse VFX sequence file '{}': {}", path, e.what());
            return std::nullopt;
        }
        catch (const std::exception& e)
        {
            vfLogError("Unexpected error loading VFX sequence '{}': {}", path, e.what());
            return std::nullopt;
        }
    }

    bool VFXSequenceAsset::save(const VFXSequenceData& data, std::string_view path)
    {
        json j;

        j["version"] = VFX_SEQUENCE_FORMAT_VERSION;
        j["uuid"] = data.uuid;
        j["name"] = data.name;

        json stepsJson = json::array();
        for (const auto& step : data.steps)
        {
            stepsJson.push_back(serializeStep(step));
        }
        j["steps"] = stepsJson;

        // VK-1451 timeline controls.
        j["seed"] = data.seed;
        j["playbackRate"] = data.playbackRate;
        j["fixedStep"] = data.fixedStep;
        j["prewarm"] = data.prewarm;

        json markersJson = json::array();
        for (const auto& marker : data.eventMarkers)
        {
            json m;
            m["time"] = marker.time;
            m["cueName"] = marker.cueName;
            if (!payloadEmpty(marker.payload))
                m["payload"] = serializePayload(marker.payload);
            markersJson.push_back(std::move(m));
        }
        j["eventMarkers"] = markersJson;

        try
        {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to create VFX sequence file: {}", path);
                return false;
            }

            file << j.dump(4);
            vfLogInfo("Saved VFX sequence: {} to {}", data.name, path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save VFX sequence file {}: {}", path, e.what());
            return false;
        }
    }

    VFXSequenceData VFXSequenceAsset::createDefault(const std::string& name)
    {
        VFXSequenceData data;
        data.version = VFX_SEQUENCE_FORMAT_VERSION;
        data.uuid = std::to_string(uuid::UUID().getValue());
        data.name = name;
        // steps intentionally left empty
        return data;
    }
}
