#include "../print/Log.hpp"
#include "VFXAsset.hpp"
#include "../uuid/UUID.hpp"
#include <nlohmann/json.hpp>
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

            std::ifstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to open VFX file: {}", path);
                return std::nullopt;
            }

            json j;
            try
            {
                file >> j;
            }
            catch (const json::parse_error& e)
            {
                vfLogError("VFX file '{}' contains invalid JSON at byte {}: {}",
                           path, e.byte, e.what());
                return std::nullopt;
            }

            if (!j.is_object())
            {
                vfLogError("VFX file '{}' must contain a JSON object at root level", path);
                return std::nullopt;
            }

            return j;
        }

        VFXNode createDefaultEmitterNode(uint32_t nodeId)
        {
            VFXNode emitterNode;
            emitterNode.id = nodeId;
            emitterNode.type = VFXNodeType::Emitter;
            emitterNode.name = "Emitter";
            emitterNode.position = glm::vec2(100.0f, 200.0f);

            emitterNode.properties["spawnRate"] = VFXProperty{
                "spawnRate", VFXPropertyType::Float,
                EmitterDefaults::SPAWN_RATE, 0.0f, 1000.0f
            };
            emitterNode.properties["lifetime"] = VFXProperty{
                "lifetime", VFXPropertyType::Float,
                EmitterDefaults::LIFETIME, 0.0f, 60.0f
            };
            emitterNode.properties["startSize"] = VFXProperty{
                "startSize", VFXPropertyType::Float,
                EmitterDefaults::START_SIZE, 0.0f, 100.0f
            };
            emitterNode.properties["startVelocity"] = VFXProperty{
                "startVelocity", VFXPropertyType::Vec3,
                glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 0.0f
            };
            emitterNode.properties["startColor"] = VFXProperty{
                "startColor", VFXPropertyType::Color,
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 0.0f, 1.0f
            };
            emitterNode.properties["looping"] = VFXProperty{
                "looping", VFXPropertyType::Bool,
                EmitterDefaults::LOOPING, 0.0f, 1.0f
            };
            emitterNode.properties["texture"] = VFXProperty{
                "texture", VFXPropertyType::String,
                std::string(""), 0.0f, 0.0f
            };

            emitterNode.properties["flipbookRows"] = VFXProperty{
                "flipbookRows", VFXPropertyType::Int,
                EmitterDefaults::FLIPBOOK_ROWS, 1.0f, 16.0f
            };
            emitterNode.properties["flipbookColumns"] = VFXProperty{
                "flipbookColumns", VFXPropertyType::Int,
                EmitterDefaults::FLIPBOOK_COLUMNS, 1.0f, 16.0f
            };
            emitterNode.properties["flipbookFrameRate"] = VFXProperty{
                "flipbookFrameRate", VFXPropertyType::Float,
                EmitterDefaults::FLIPBOOK_FRAME_RATE, 0.0f, 120.0f
            };
            emitterNode.properties["flipbookRandomStart"] = VFXProperty{
                "flipbookRandomStart", VFXPropertyType::Bool,
                EmitterDefaults::FLIPBOOK_RANDOM_START, 0.0f, 1.0f
            };

            // Rendering
            emitterNode.properties["alphaClipThreshold"] = VFXProperty{
                "alphaClipThreshold", VFXPropertyType::Float,
                EmitterDefaults::ALPHA_CLIP_THRESHOLD, 0.0f, 1.0f
            };
            emitterNode.properties["additiveBlend"] = VFXProperty{
                "additiveBlend", VFXPropertyType::Bool,
                EmitterDefaults::ADDITIVE_BLEND, 0.0f, 1.0f
            };

            emitterNode.properties["renderMode"] = VFXProperty{
                "renderMode", VFXPropertyType::Int,
                EmitterDefaults::RENDER_MODE, 0.0f, 4.0f
            };
            emitterNode.properties["softParticleDistance"] = VFXProperty{
                "softParticleDistance", VFXPropertyType::Float,
                EmitterDefaults::SOFT_PARTICLE_DISTANCE, 0.0f, 50.0f
            };
            emitterNode.properties["stretchMultiplier"] = VFXProperty{
                "stretchMultiplier", VFXPropertyType::Float,
                EmitterDefaults::STRETCH_MULTIPLIER, 0.1f, 10.0f
            };

            emitterNode.properties["meshPath"] = VFXProperty{
                "meshPath", VFXPropertyType::String,
                std::string(""), 0.0f, 0.0f
            };

            emitterNode.properties["maxTrailPoints"] = VFXProperty{
                "maxTrailPoints", VFXPropertyType::Int,
                EmitterDefaults::MAX_TRAIL_POINTS, 2.0f, 256.0f
            };
            emitterNode.properties["ribbonWidth"] = VFXProperty{
                "ribbonWidth", VFXPropertyType::Float,
                EmitterDefaults::RIBBON_WIDTH, 0.01f, 10.0f
            };
            emitterNode.properties["ribbonMinDistance"] = VFXProperty{
                "ribbonMinDistance", VFXPropertyType::Float,
                EmitterDefaults::RIBBON_MIN_DISTANCE, 0.0f, 5.0f
            };

            emitterNode.properties["uvScrollSpeedU"] = VFXProperty{
                "uvScrollSpeedU", VFXPropertyType::Float,
                EmitterDefaults::UV_SCROLL_SPEED_U, -10.0f, 10.0f
            };
            emitterNode.properties["uvScrollSpeedV"] = VFXProperty{
                "uvScrollSpeedV", VFXPropertyType::Float,
                EmitterDefaults::UV_SCROLL_SPEED_V, -10.0f, 10.0f
            };

            // Events
            emitterNode.properties["eventOnSpawnEnabled"] = VFXProperty{
                "eventOnSpawnEnabled", VFXPropertyType::Bool,
                EmitterDefaults::EVENT_ON_SPAWN_ENABLED, 0.0f, 1.0f
            };
            emitterNode.properties["eventOnSpawnVFX"] = VFXProperty{
                "eventOnSpawnVFX", VFXPropertyType::String,
                std::string(""), 0.0f, 0.0f
            };
            emitterNode.properties["eventOnDeathEnabled"] = VFXProperty{
                "eventOnDeathEnabled", VFXPropertyType::Bool,
                EmitterDefaults::EVENT_ON_DEATH_ENABLED, 0.0f, 1.0f
            };
            emitterNode.properties["eventOnDeathVFX"] = VFXProperty{
                "eventOnDeathVFX", VFXPropertyType::String,
                std::string(""), 0.0f, 0.0f
            };
            emitterNode.properties["eventOnCollisionEnabled"] = VFXProperty{
                "eventOnCollisionEnabled", VFXPropertyType::Bool,
                EmitterDefaults::EVENT_ON_COLLISION_ENABLED, 0.0f, 1.0f
            };
            emitterNode.properties["eventOnCollisionVFX"] = VFXProperty{
                "eventOnCollisionVFX", VFXPropertyType::String,
                std::string(""), 0.0f, 0.0f
            };
            emitterNode.properties["eventOnLifetimeThresholdEnabled"] = VFXProperty{
                "eventOnLifetimeThresholdEnabled", VFXPropertyType::Bool,
                EmitterDefaults::EVENT_ON_LIFETIME_THRESHOLD_ENABLED, 0.0f, 1.0f
            };
            emitterNode.properties["eventOnLifetimeThresholdVFX"] = VFXProperty{
                "eventOnLifetimeThresholdVFX", VFXPropertyType::String,
                std::string(""), 0.0f, 0.0f
            };
            emitterNode.properties["eventLifetimeThreshold"] = VFXProperty{
                "eventLifetimeThreshold", VFXPropertyType::Float,
                EmitterDefaults::EVENT_LIFETIME_THRESHOLD, 0.0f, 1.0f
            };

            // Collision
            emitterNode.properties["collisionEnabled"] = VFXProperty{
                "collisionEnabled", VFXPropertyType::Bool,
                EmitterDefaults::COLLISION_ENABLED, 0.0f, 1.0f
            };
            emitterNode.properties["collisionBounce"] = VFXProperty{
                "collisionBounce", VFXPropertyType::Float,
                EmitterDefaults::COLLISION_BOUNCE, 0.0f, 1.0f
            };
            emitterNode.properties["collisionFriction"] = VFXProperty{
                "collisionFriction", VFXPropertyType::Float,
                EmitterDefaults::COLLISION_FRICTION, 0.0f, 1.0f
            };
            emitterNode.properties["collisionLifetimeLoss"] = VFXProperty{
                "collisionLifetimeLoss", VFXPropertyType::Float,
                EmitterDefaults::COLLISION_LIFETIME_LOSS, 0.0f, 1.0f
            };

            return emitterNode;
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

        // Serialize nodes
        json nodesJson = json::array();
        for (const auto& node : vfxData.graph.nodes)
        {
            nodesJson.push_back(serializeNode(node));
        }
        graphJson["nodes"] = nodesJson;

        // Serialize links
        json linksJson = json::array();
        for (const auto& link : vfxData.graph.links)
        {
            linksJson.push_back(serializeLink(link));
        }
        graphJson["links"] = linksJson;

        j["graph"] = graphJson;

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

            file << j.dump(4); // Pretty print with 4-space indent
            vfLogInfo("Saved VFX: {} to {}", vfxData.name, path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save VFX file {}: {}", path, e.what());
            return false;
        }
    }

    VFXData VFXAsset::createDefault(const std::string& name)
    {
        VFXData vfxData;
        vfxData.uuid = std::to_string(uuid::UUID().getValue());
        vfxData.name = name;
        vfxData.version = VFX_FORMAT_VERSION;

        vfxData.graph.nodes.push_back(createDefaultEmitterNode(vfxData.graph.nextNodeId++));

        // Create OutSystem node
        VFXNode outNode;
        outNode.id = vfxData.graph.nextNodeId++;
        outNode.type = VFXNodeType::OutSystem;
        outNode.name = "Output";
        outNode.position = glm::vec2(400.0f, 200.0f);
        vfxData.graph.nodes.push_back(std::move(outNode));

        // Create link from Emitter to OutSystem
        VFXNodeLink link;
        link.id = vfxData.graph.nextLinkId++;
        link.sourceNodeId = 1;  // Emitter
        link.targetNodeId = 2;  // OutSystem
        link.sourcePin = "Output";
        link.targetPin = "Input";
        vfxData.graph.links.push_back(std::move(link));

        return vfxData;
    }
}
