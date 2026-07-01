#include "../print/Log.hpp"
#include "../uuid/UUID.hpp"
#include "VFXAsset.hpp"
#include "../resource/VFSHelpers.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <format>

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

        bool validateVFXFileAccess(const fs::path& filePath, std::string_view path)
        {
            if (!fs::exists(filePath))
            {
                vfLogError("VFX file not found: {}", path);
                return false;
            }

            std::error_code ec;
            auto fileSize = fs::file_size(filePath, ec);
            if (ec)
            {
                vfLogError("Cannot read VFX file size '{}': {}", path, ec.message());
                return false;
            }

            constexpr size_t MAX_VFX_FILE_SIZE = 10 * 1024 * 1024; // 10 MB limit
            if (fileSize > MAX_VFX_FILE_SIZE)
            {
                vfLogError("VFX file '{}' is too large ({} bytes, max {} bytes)",
                           path, fileSize, MAX_VFX_FILE_SIZE);
                return false;
            }

            return true;
        }

        std::optional<json> readAndParseVFXFile(std::string_view path)
        {
            fs::path filePath(path);
            if (!validateVFXFileAccess(filePath, path))
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
                vfLogError("VFX file '{}' contains invalid JSON at byte {}: {}",
                           path, e.byte, e.what());
                return std::nullopt;
            }
            if (j.is_null())
            {
                vfLogError("Failed to open VFX file: {}", path);
                return std::nullopt;
            }

            if (!j.is_object())
            {
                vfLogError("VFX file '{}' must contain a JSON object at root level", path);
                return std::nullopt;
            }

            return j;
        }

        json vec3ToJson(const glm::vec3& v)
        {
            return json::array({v.x, v.y, v.z});
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

    json VFXAsset::serializePropertyValue(const VFXPropertyValue& val, VFXPropertyType type)
    {
        switch (type)
        {
        case VFXPropertyType::Float:
            return std::holds_alternative<float>(val) ? std::get<float>(val) : 0.0f;
        case VFXPropertyType::Vec2:
            if (std::holds_alternative<glm::vec2>(val))
            {
                const auto& v = std::get<glm::vec2>(val);
                return json::array({v.x, v.y});
            }
            return json::array({0.0f, 0.0f});
        case VFXPropertyType::Vec3:
            if (std::holds_alternative<glm::vec3>(val))
            {
                const auto& v = std::get<glm::vec3>(val);
                return json::array({v.x, v.y, v.z});
            }
            return json::array({0.0f, 0.0f, 0.0f});
        case VFXPropertyType::Vec4:
        case VFXPropertyType::Color:
            if (std::holds_alternative<glm::vec4>(val))
            {
                const auto& v = std::get<glm::vec4>(val);
                return json::array({v.x, v.y, v.z, v.w});
            }
            return json::array({1.0f, 1.0f, 1.0f, 1.0f});
        case VFXPropertyType::Int:
            return std::holds_alternative<int32_t>(val) ? std::get<int32_t>(val) : 0;
        case VFXPropertyType::Bool:
            return std::holds_alternative<bool>(val) ? std::get<bool>(val) : false;
        case VFXPropertyType::String:
            return std::holds_alternative<std::string>(val) ? std::get<std::string>(val) : "";
        case VFXPropertyType::Curve:
            if (auto* curve = std::get_if<VFXCurve>(&val))
            {
                json cj;
                json keysArr = json::array();
                for (const auto& key : curve->keys)
                {
                    keysArr.push_back(json::array({key.time, key.value, key.inTangent, key.outTangent}));
                }
                cj["keys"] = keysArr;
                return cj;
            }
            return json::object();
        case VFXPropertyType::Gradient:
            if (auto* grad = std::get_if<VFXGradient>(&val))
            {
                json gj;
                json stopsArr = json::array();
                for (const auto& stop : grad->stops)
                {
                    stopsArr.push_back(json::array({stop.position, stop.color.r, stop.color.g, stop.color.b, stop.color.a}));
                }
                gj["stops"] = stopsArr;
                return gj;
            }
            return json::object();
        default:
            return 0.0f;
        }
    }

    VFXPropertyValue VFXAsset::deserializePropertyValue(const json& j, VFXPropertyType type)
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
                return j.is_number_integer() ? j.get<int32_t>() : 0;
            case VFXPropertyType::Bool:
                return j.is_boolean() ? j.get<bool>() : false;
            case VFXPropertyType::String:
                return j.is_string() ? j.get<std::string>() : std::string("");
            case VFXPropertyType::Curve:
            {
                VFXCurve curve;
                if (j.is_object() && j.contains("keys") && j["keys"].is_array())
                {
                    for (const auto& keyArr : j["keys"])
                    {
                        if (keyArr.is_array() && keyArr.size() >= 4)
                        {
                            curve.keys.push_back({
                                keyArr[0].get<float>(), keyArr[1].get<float>(),
                                keyArr[2].get<float>(), keyArr[3].get<float>()
                            });
                        }
                    }
                }
                return curve;
            }
            case VFXPropertyType::Gradient:
            {
                VFXGradient gradient;
                if (j.is_object() && j.contains("stops") && j["stops"].is_array())
                {
                    for (const auto& stopArr : j["stops"])
                    {
                        if (stopArr.is_array() && stopArr.size() >= 5)
                        {
                            gradient.stops.push_back({
                                stopArr[0].get<float>(),
                                glm::vec4(stopArr[1].get<float>(), stopArr[2].get<float>(),
                                          stopArr[3].get<float>(), stopArr[4].get<float>())
                            });
                        }
                    }
                }
                return gradient;
            }
            default:
                return 0.0f;
            }
        }
        catch (const json::exception&)
        {
            return 0.0f;
        }
    }

    json VFXAsset::serializeProperty(const VFXProperty& prop)
    {
        json j;
        j["type"] = propertyTypeToString(prop.type);
        j["value"] = serializePropertyValue(prop.value, prop.type);
        if (prop.type == VFXPropertyType::Float || prop.type == VFXPropertyType::Int ||
            prop.type == VFXPropertyType::Curve)
        {
            j["min"] = prop.min;
            j["max"] = prop.max;
        }
        return j;
    }

    VFXProperty VFXAsset::deserializeProperty(const json& j, const std::string& propName)
    {
        VFXProperty prop;
        prop.name = propName;
        prop.type = stringToPropertyType(j.value("type", "Float"));
        if (j.contains("value"))
        {
            prop.value = deserializePropertyValue(j["value"], prop.type);
        }
        prop.min = j.value("min", 0.0f);
        prop.max = j.value("max", 1.0f);
        return prop;
    }

    json VFXAsset::serializeNode(const VFXNode& node)
    {
        json j;
        j["id"] = node.id;
        j["type"] = nodeTypeToString(node.type);
        j["name"] = node.name;
        j["position"] = json::array({node.position.x, node.position.y});

        json propsJson = json::object();
        for (const auto& [key, prop] : node.properties)
        {
            propsJson[key] = serializeProperty(prop);
        }
        j["properties"] = propsJson;

        return j;
    }

    VFXNode VFXAsset::deserializeNode(const json& j)
    {
        VFXNode node;
        node.id = j.value("id", 0u);
        node.type = stringToNodeType(j.value("type", "Emitter"));
        node.name = j.value("name", "");

        if (j.contains("position") && j["position"].is_array() && j["position"].size() >= 2)
        {
            node.position.x = j["position"][0].get<float>();
            node.position.y = j["position"][1].get<float>();
        }

        if (j.contains("properties") && j["properties"].is_object())
        {
            for (auto& [key, propJson] : j["properties"].items())
            {
                node.properties[key] = deserializeProperty(propJson, key);
            }
        }

        return node;
    }

    json VFXAsset::serializeLink(const VFXNodeLink& link)
    {
        json j;
        j["id"] = link.id;
        j["sourceNode"] = link.sourceNodeId;
        j["targetNode"] = link.targetNodeId;
        j["sourcePin"] = link.sourcePin;
        j["targetPin"] = link.targetPin;
        return j;
    }

    VFXNodeLink VFXAsset::deserializeLink(const json& j)
    {
        VFXNodeLink link;
        link.id = j.value("id", 0u);
        link.sourceNodeId = j.value("sourceNode", 0u);
        link.targetNodeId = j.value("targetNode", 0u);
        link.sourcePin = j.value("sourcePin", "");
        link.targetPin = j.value("targetPin", "");
        return link;
    }

    void VFXAsset::parseVFXNodes(const json& graphJson, VFXGraph& graph,
                                    const WarningLogger& logWarning)
    {
        if (!graphJson.contains("nodes") || !graphJson["nodes"].is_array())
        {
            return;
        }

        for (size_t i = 0; i < graphJson["nodes"].size(); ++i)
        {
            const auto& nodeJson = graphJson["nodes"][i];
            if (!nodeJson.is_object())
            {
                logWarning(std::format("Node at index {} is not an object, skipping", i));
                continue;
            }
            VFXNode node = deserializeNode(nodeJson);
            graph.nextNodeId = std::max(graph.nextNodeId, node.id + 1);
            graph.nodes.push_back(std::move(node));
        }
    }

    void VFXAsset::parseVFXLinks(const json& graphJson, VFXGraph& graph,
                                    const WarningLogger& logWarning)
    {
        if (!graphJson.contains("links") || !graphJson["links"].is_array())
        {
            return;
        }

        for (size_t i = 0; i < graphJson["links"].size(); ++i)
        {
            const auto& linkJson = graphJson["links"][i];
            if (!linkJson.is_object())
            {
                logWarning(std::format("Link at index {} is not an object, skipping", i));
                continue;
            }
            VFXNodeLink link = deserializeLink(linkJson);
            graph.nextLinkId = std::max(graph.nextLinkId, link.id + 1);
            graph.links.push_back(std::move(link));
        }
    }

    std::optional<VFXData> VFXAsset::load(std::string_view path)
    {
        auto parsedJson = readAndParseVFXFile(path);
        if (!parsedJson)
            return std::nullopt;

        const json& j = *parsedJson;
        VFXData vfxData;
        int warningCount = 0;
        auto logWarning = createWarningLogger(warningCount, 20);

        try
        {
            vfxData.version = j.value("version", VFX_FORMAT_VERSION);
            vfxData.uuid = j.value("uuid", std::to_string(uuid::UUID().getValue()));
            vfxData.name = j.value("name", "Unnamed VFX");

            if (vfxData.name.empty())
            {
                vfxData.name = "Unnamed VFX";
                logWarning("VFX has empty name, using default");
            }

            if (j.contains("graph") && j["graph"].is_object())
            {
                const auto& graphJson = j["graph"];
                parseVFXNodes(graphJson, vfxData.graph, logWarning);
                parseVFXLinks(graphJson, vfxData.graph, logWarning);
            }

            // VK-1453 (Phase 4) — bounds / scalability / cull opt-in (all tolerant;
            // absent keys keep the struct defaults: Auto bounds, disabled, off).
            if (j.contains("bounds") && j["bounds"].is_object())
            {
                const auto& boundsJson = j["bounds"];
                vfxData.bounds.mode = static_cast<VFXBoundsMode>(
                    static_cast<uint8_t>(boundsJson.value("mode", 0)));
                if (boundsJson.contains("center"))
                    vfxData.bounds.center = jsonToVec3(boundsJson["center"], glm::vec3(0.0f));
                if (boundsJson.contains("extents"))
                    vfxData.bounds.extents = jsonToVec3(boundsJson["extents"], glm::vec3(0.0f));
            }

            if (j.contains("scalability") && j["scalability"].is_object())
            {
                const auto& scalabilityJson = j["scalability"];
                vfxData.scalability.enabled = scalabilityJson.value("enabled", false);
                if (scalabilityJson.contains("levels") && scalabilityJson["levels"].is_array())
                {
                    const auto& levelsJson = scalabilityJson["levels"];
                    const size_t count = std::min<size_t>(levelsJson.size(), kVFXQualityTierCount);
                    for (size_t i = 0; i < count; ++i)
                    {
                        const auto& levelJson = levelsJson[i];
                        if (!levelJson.is_object())
                            continue;
                        VFXScalabilityLevel& level = vfxData.scalability.levels[i];
                        level.spawnRateScale = levelJson.value("spawnRateScale", 1.0f);
                        level.maxParticles = levelJson.value("maxParticles", -1);
                        level.cullDistance = levelJson.value("cullDistance", -1.0f);
                        level.updateInterval = levelJson.value("updateInterval", 1);
                        level.rendererEnabled = levelJson.value("rendererEnabled", true);
                    }
                }
            }

            vfxData.cullEligible = j.value("cullEligible", false);

            if (warningCount > 0)
                vfLogWarning("Loaded VFX '{}' with {} warning(s)", vfxData.name, warningCount);

            return vfxData;
        }
        catch (const json::exception& e)
        {
            vfLogError("Failed to parse VFX file '{}': {}", path, e.what());
            return std::nullopt;
        }
        catch (const std::exception& e)
        {
            vfLogError("Unexpected error loading VFX '{}': {}", path, e.what());
            return std::nullopt;
        }
    }

    bool VFXAsset::save(std::string_view path, const VFXData& vfxData)
    {
        json j;

        j["version"] = VFX_FORMAT_VERSION;
        j["uuid"] = vfxData.uuid;
        j["name"] = vfxData.name;

        json graphJson;

        json nodesJson = json::array();
        for (const auto& node : vfxData.graph.nodes)
        {
            nodesJson.push_back(serializeNode(node));
        }
        graphJson["nodes"] = nodesJson;

        json linksJson = json::array();
        for (const auto& link : vfxData.graph.links)
        {
            linksJson.push_back(serializeLink(link));
        }
        graphJson["links"] = linksJson;

        j["graph"] = graphJson;

        // VK-1453 (Phase 4) — explicit bounds, per-tier scalability and cull opt-in.
        {
            json boundsJson;
            boundsJson["mode"] = static_cast<int>(static_cast<uint8_t>(vfxData.bounds.mode));
            boundsJson["center"] = vec3ToJson(vfxData.bounds.center);
            boundsJson["extents"] = vec3ToJson(vfxData.bounds.extents);
            j["bounds"] = std::move(boundsJson);
        }

        {
            json scalabilityJson;
            scalabilityJson["enabled"] = vfxData.scalability.enabled;
            json levelsJson = json::array();
            for (const auto& level : vfxData.scalability.levels)
            {
                json levelJson;
                levelJson["spawnRateScale"] = level.spawnRateScale;
                levelJson["maxParticles"] = level.maxParticles;
                levelJson["cullDistance"] = level.cullDistance;
                levelJson["updateInterval"] = level.updateInterval;
                levelJson["rendererEnabled"] = level.rendererEnabled;
                levelsJson.push_back(std::move(levelJson));
            }
            scalabilityJson["levels"] = std::move(levelsJson);
            j["scalability"] = std::move(scalabilityJson);
        }

        j["cullEligible"] = vfxData.cullEligible;

        try
        {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to create VFX file: {}", path);
                return false;
            }

            file << j.dump(4);
            vfLogInfo("Saved VFX: {} to {}", vfxData.name, path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save VFX file {}: {}", path, e.what());
            return false;
        }
    }

}
