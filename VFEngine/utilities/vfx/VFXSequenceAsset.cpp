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

        json scalarArr = json::array();
        for (const auto& [name, value] : step.scalarOverrides)
        {
            scalarArr.push_back(json::array({name, value}));
        }
        j["scalarOverrides"] = scalarArr;

        json vectorArr = json::array();
        for (const auto& [name, value] : step.vectorOverrides)
        {
            vectorArr.push_back(json::array({name, vec4ToJson(value)}));
        }
        j["vectorOverrides"] = vectorArr;

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

        if (j.contains("scalarOverrides") && j["scalarOverrides"].is_array())
        {
            for (const auto& entry : j["scalarOverrides"])
            {
                if (entry.is_array() && entry.size() >= 2 &&
                    entry[0].is_string() && entry[1].is_number())
                {
                    step.scalarOverrides.emplace_back(entry[0].get<std::string>(),
                                                      entry[1].get<float>());
                }
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
                    step.vectorOverrides.emplace_back(
                        entry[0].get<std::string>(),
                        glm::vec4(v[0].get<float>(), v[1].get<float>(),
                                  v[2].get<float>(), v[3].get<float>()));
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
