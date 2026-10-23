#include "../print/Log.hpp"
#include "../uuid/UUID.hpp"
#include "VFXSequenceAsset.hpp"
#include "VFXParamOverrideJson.hpp"
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
                   !payload.velocity.has_value() &&
                   !payload.normal.has_value() &&
                   !payload.color.has_value() &&
                   !payload.scalar.has_value() &&
                   payload.custom.empty();
        }

        json serializePayload(const VFXCuePayload& payload)
        {
            json j;
            if (payload.position)
                j["position"] = vec3ToJson(*payload.position);
            if (payload.velocity)
                j["velocity"] = vec3ToJson(*payload.velocity);
            if (payload.normal)
                j["normal"] = vec3ToJson(*payload.normal);
            if (payload.color)
                j["color"] = vec4ToJson(*payload.color);
            if (payload.scalar)
                j["scalar"] = *payload.scalar;
            if (!payload.custom.empty())
            {
                json custom = json::array();
                for (const auto& overrideValue : payload.custom)
                    custom.push_back(paramOverrideToJson(overrideValue));
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
            if (j.contains("velocity"))
                payload.velocity = jsonToVec3(j["velocity"], glm::vec3(0.0f));
            if (j.contains("normal"))
                payload.normal = jsonToVec3(j["normal"], glm::vec3(0.0f));
            if (j.contains("color"))
                payload.color = jsonToVec4(j["color"], glm::vec4(1.0f));
            if (j.contains("scalar") && j["scalar"].is_number())
                payload.scalar = j["scalar"].get<float>();
            if (j.contains("custom") && j["custom"].is_array())
            {
                for (const auto& entry : j["custom"])
                {
                    if (auto overrideValue = paramOverrideFromJson(entry))
                        payload.custom.push_back(std::move(*overrideValue));
                }
            }
            return payload;
        }
    } // anonymous namespace

    json VFXSequenceAsset::serializeStep(const VFXSequenceStep& step)
    {
        json j;
        j["kind"] = static_cast<int>(static_cast<uint8_t>(step.kind)); // VK-1496
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
            overrides.push_back(paramOverrideToJson(overrideValue));
        j["overrides"] = overrides;

        // VK-1496 — Sound fields (written for all kinds; tolerant reader ignores when unused).
        serialization::writeAssetRef(j, "audioRef", step.audioRef);
        j["volume"] = step.volume;
        j["pitch"] = step.pitch;
        j["spatialized"] = step.spatialized;

        // VK-1496 — ScriptCue fields. cuePayload only when non-empty (matches marker codec).
        j["emitCueName"] = step.emitCueName;
        if (!payloadEmpty(step.cuePayload))
            j["cuePayload"] = serializePayload(step.cuePayload);

        // VK-1497 — deterministic per-step variety (additive; tolerant reader defaults them).
        j["probability"] = step.probability;
        j["variantGroup"] = step.variantGroup;

        // VK-1524 — trigger + source output declaration + StepOutput receiver binding (additive).
        j["trigger"] = static_cast<int>(static_cast<uint8_t>(step.trigger));
        j["outputEventName"] = step.outputEventName;
        j["outputEventType"] = static_cast<int>(static_cast<uint8_t>(step.outputEventType));
        j["sourceStepIndex"] = step.sourceStepIndex;
        j["sourceEventName"] = step.sourceEventName;
        j["eventConsumption"] = static_cast<int>(static_cast<uint8_t>(step.eventConsumption));
        j["eventBudget"] = step.eventBudget;
        j["inheritVelocity"] = step.inheritVelocity;
        j["inheritColor"] = step.inheritColor;
        j["inheritScalar"] = step.inheritScalar;
        j["inheritNormal"] = step.inheritNormal;

        return j;
    }

    std::optional<VFXSequenceStep> VFXSequenceAsset::deserializeStep(const json& j)
    {
        VFXSequenceStep step;

        // VK-1496 — read `kind` BEFORE the vfxRef gate. Absent in v1.3 files => VFX, so
        // the gate stays byte-identical for old assets. Only VFX steps require a vfxRef;
        // Sound/ScriptCue (and unknown future kinds) legitimately have none.
        step.kind = static_cast<VFXStepKind>(static_cast<uint8_t>(j.value("kind", 0)));
        step.vfxRef = serialization::readAssetRef(j, "vfxRef");
        step.audioRef = serialization::readAssetRef(j, "audioRef");
        if (step.kind == VFXStepKind::VFX && !step.vfxRef.isValid())
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
                if (auto overrideValue = paramOverrideFromJson(entry))
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

        // VK-1496 — Sound + ScriptCue fields (tolerant; absent keys keep struct defaults).
        step.volume = j.value("volume", 1.0f);
        step.pitch = j.value("pitch", 1.0f);
        step.spatialized = j.value("spatialized", false);
        step.emitCueName = j.value("emitCueName", "");
        if (j.contains("cuePayload"))
            step.cuePayload = deserializePayload(j["cuePayload"]);

        // VK-1497 — per-step variety (tolerant; defaults MUST match struct defaults so a
        // pre-VK-1497 file loads exactly like a default-constructed step: always play, no group).
        step.probability = j.value("probability", 1.0f);
        step.variantGroup = j.value("variantGroup", -1);

        // VK-1524 — trigger (back-compat: pre-1.6 files have no `trigger` key; derive it from
        // `cueName` so time/cue behavior is byte-identical) + source output + StepOutput binding.
        const int defaultTrigger = static_cast<int>(static_cast<uint8_t>(
            step.cueName.empty() ? VFXStepTrigger::Time : VFXStepTrigger::Cue));
        step.trigger = static_cast<VFXStepTrigger>(
            static_cast<uint8_t>(j.value("trigger", defaultTrigger)));
        step.outputEventName = j.value("outputEventName", "");
        step.outputEventType = static_cast<VFXEventType>(static_cast<uint8_t>(
            j.value("outputEventType", static_cast<int>(static_cast<uint8_t>(VFXEventType::OnDeath)))));
        step.sourceStepIndex = j.value("sourceStepIndex", -1);
        step.sourceEventName = j.value("sourceEventName", "");
        step.eventConsumption = static_cast<VFXEventConsumption>(
            static_cast<uint8_t>(j.value("eventConsumption", 0)));
        step.eventBudget = j.value("eventBudget", 16u);
        step.inheritVelocity = j.value("inheritVelocity", false);
        step.inheritColor = j.value("inheritColor", false);
        step.inheritScalar = j.value("inheritScalar", false);
        step.inheritNormal = j.value("inheritNormal", false);

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

            // VK-1453 (Phase 4) — aggregate bounds (tolerant; absent => Auto default).
            if (j.contains("bounds") && j["bounds"].is_object())
            {
                const auto& boundsJson = j["bounds"];
                data.bounds.mode = static_cast<VFXBoundsMode>(
                    static_cast<uint8_t>(boundsJson.value("mode", 0)));
                if (boundsJson.contains("center"))
                    data.bounds.center = jsonToVec3(boundsJson["center"], glm::vec3(0.0f));
                if (boundsJson.contains("extents"))
                    data.bounds.extents = jsonToVec3(boundsJson["extents"], glm::vec3(0.0f));
            }

            // VK-1498 — stable-loop flag (tolerant; absent => false = re-roll variety each loop).
            data.stableLoop = j.value("stableLoop", false);

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

        // VK-1453 (Phase 4) — aggregate bounds over the sequence's steps.
        {
            json boundsJson;
            boundsJson["mode"] = static_cast<int>(static_cast<uint8_t>(data.bounds.mode));
            boundsJson["center"] = vec3ToJson(data.bounds.center);
            boundsJson["extents"] = vec3ToJson(data.bounds.extents);
            j["bounds"] = std::move(boundsJson);
        }

        // VK-1498 — stable-loop flag.
        j["stableLoop"] = data.stableLoop;

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
