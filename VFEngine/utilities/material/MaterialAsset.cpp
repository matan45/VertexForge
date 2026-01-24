#include "MaterialAsset.hpp"
#include "../print/EditorLogger.hpp"
#include "../uuid/UUID.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <format>

namespace material
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    json MaterialAsset::serializeProperty(const NodeProperty& prop)
    {
        return std::visit([](auto&& arg) -> json
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, float>)
            {
                return arg;
            }
            else if constexpr (std::is_same_v<T, glm::vec2>)
            {
                return json::array({arg.x, arg.y});
            }
            else if constexpr (std::is_same_v<T, glm::vec3>)
            {
                return json::array({arg.x, arg.y, arg.z});
            }
            else if constexpr (std::is_same_v<T, glm::vec4>)
            {
                return json::array({arg.x, arg.y, arg.z, arg.w});
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                return arg;
            }
            return json();
        }, prop);
    }

    NodeProperty MaterialAsset::deserializeProperty(const json& j, const std::string& context)
    {
        try
        {
            if (j.is_number())
            {
                return j.get<float>();
            }
            else if (j.is_string())
            {
                return j.get<std::string>();
            }
            else if (j.is_array())
            {
                // Validate array elements are numbers
                for (size_t i = 0; i < j.size(); ++i)
                {
                    if (!j[i].is_number())
                    {
                        vfLogWarning("Invalid array element at index {} in property{}, using default",
                                     i, context.empty() ? "" : " '" + context + "'");
                        return 0.0f;
                    }
                }
                if (j.size() == 2)
                {
                    return glm::vec2(j[0].get<float>(), j[1].get<float>());
                }
                else if (j.size() == 3)
                {
                    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
                }
                else if (j.size() == 4)
                {
                    return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
                }
                else
                {
                    vfLogWarning("Unexpected array size {} in property{}, using default",
                                 j.size(), context.empty() ? "" : " '" + context + "'");
                }
            }
            else if (!j.is_null())
            {
                vfLogWarning("Unexpected JSON type for property{}, using default",
                             context.empty() ? "" : " '" + context + "'");
            }
        }
        catch (const json::exception& e)
        {
            vfLogWarning("Failed to parse property{}: {}",
                         context.empty() ? "" : " '" + context + "'", e.what());
        }
        return 0.0f; // Default
    }

    json MaterialAsset::serializeParamValue(const ParameterValue& val)
    {
        return std::visit([](auto&& arg) -> json
        {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, float>)
            {
                return arg;
            }
            else if constexpr (std::is_same_v<T, glm::vec2>)
            {
                return json::array({arg.x, arg.y});
            }
            else if constexpr (std::is_same_v<T, glm::vec3>)
            {
                return json::array({arg.x, arg.y, arg.z});
            }
            else if constexpr (std::is_same_v<T, glm::vec4>)
            {
                return json::array({arg.x, arg.y, arg.z, arg.w});
            }
            return json();
        }, val);
    }

    ParameterValue MaterialAsset::deserializeParamValue(const json& j, ParameterType type, const std::string& paramName)
    {
        try
        {
            switch (type)
            {
            case ParameterType::Scalar:
                if (!j.is_number())
                {
                    vfLogWarning("Parameter '{}' expected scalar, got different type", paramName);
                    return 0.0f;
                }
                return j.get<float>();
            case ParameterType::Vec2:
                if (!j.is_array() || j.size() < 2)
                {
                    vfLogWarning("Parameter '{}' expected vec2 array, using default", paramName);
                    return glm::vec2(0.0f);
                }
                return glm::vec2(j[0].get<float>(), j[1].get<float>());
            case ParameterType::Vec3:
                if (!j.is_array() || j.size() < 3)
                {
                    vfLogWarning("Parameter '{}' expected vec3 array, using default", paramName);
                    return glm::vec3(0.0f);
                }
                return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
            case ParameterType::Vec4:
            case ParameterType::Color:
                if (!j.is_array() || j.size() < 4)
                {
                    vfLogWarning("Parameter '{}' expected vec4 array, using default", paramName);
                    return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                }
                return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
            default:
                return 0.0f;
            }
        }
        catch (const json::exception& e)
        {
            vfLogWarning("Failed to parse parameter '{}': {}", paramName, e.what());
            // Return type-appropriate default
            switch (type)
            {
            case ParameterType::Vec2: return glm::vec2(0.0f);
            case ParameterType::Vec3: return glm::vec3(0.0f);
            case ParameterType::Vec4:
            case ParameterType::Color: return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
            default: return 0.0f;
            }
        }
    }

    std::optional<MaterialData> MaterialAsset::load(std::string_view path)
    {
        fs::path filePath(path);

        // Validate file exists
        if (!fs::exists(filePath))
        {
            vfLogError("Material file not found: {}", path);
            return std::nullopt;
        }

        // Check file size (sanity check - materials shouldn't be huge)
        std::error_code ec;
        auto fileSize = fs::file_size(filePath, ec);
        if (ec)
        {
            vfLogError("Cannot read material file size '{}': {}", path, ec.message());
            return std::nullopt;
        }
        constexpr size_t MAX_MATERIAL_FILE_SIZE = 10 * 1024 * 1024; // 10 MB limit
        if (fileSize > MAX_MATERIAL_FILE_SIZE)
        {
            vfLogError("Material file '{}' is too large ({} bytes, max {} bytes)",
                       path, fileSize, MAX_MATERIAL_FILE_SIZE);
            return std::nullopt;
        }

        std::ifstream file(filePath);
        if (!file.is_open())
        {
            vfLogError("Failed to open material file: {}", path);
            return std::nullopt;
        }

        // Parse JSON with detailed error handling
        json j;
        try
        {
            file >> j;
        }
        catch (const json::parse_error& e)
        {
            vfLogError("Material file '{}' contains invalid JSON at byte {}: {}",
                       path, e.byte, e.what());
            return std::nullopt;
        }

        // Validate root is an object
        if (!j.is_object())
        {
            vfLogError("Material file '{}' must contain a JSON object at root level", path);
            return std::nullopt;
        }

        MaterialData material;
        int warningCount = 0;
        constexpr int MAX_WARNINGS = 20; // Limit warning spam

        auto logWarningLimited = [&](const std::string& msg)
        {
            if (warningCount < MAX_WARNINGS)
            {
                vfLogWarning("{}", msg);
                warningCount++;
                if (warningCount == MAX_WARNINGS)
                {
                    vfLogWarning("(suppressing further warnings for this file)");
                }
            }
        };

        try
        {
            // Check version compatibility
            std::string fileVersion = j.value("version", MATERIAL_FORMAT_VERSION);
            bool needsMigration = (fileVersion != MATERIAL_FORMAT_VERSION);
            if (needsMigration)
            {
                logWarningLimited(std::format(
                    "Material file '{}' has version {} (current is {}). Will migrate on save.",
                    std::string(path), fileVersion, MATERIAL_FORMAT_VERSION));
            }

            // Basic properties with validation
            material.uuid = j.value("uuid", std::to_string(uuid::UUID().getValue()));
            material.name = j.value("name", "Unnamed Material");

            if (material.name.empty())
            {
                material.name = "Unnamed Material";
                logWarningLimited("Material has empty name, using default");
            }

            material.blendMode = stringToBlendMode(j.value("blendMode", "opaque"));

            // Shader graph with comprehensive validation
            if (j.contains("graph"))
            {
                if (!j["graph"].is_object())
                {
                    logWarningLimited("'graph' field is not an object, skipping graph data");
                }
                else
                {
                    const auto& graphJson = j["graph"];

                    // Parse nodes
                    if (graphJson.contains("nodes"))
                    {
                        if (!graphJson["nodes"].is_array())
                        {
                            logWarningLimited("'graph.nodes' is not an array, skipping nodes");
                        }
                        else
                        {
                            std::unordered_set<uint32_t> nodeIds; // Track for duplicate detection

                            for (size_t i = 0; i < graphJson["nodes"].size(); ++i)
                            {
                                const auto& nodeJson = graphJson["nodes"][i];

                                if (!nodeJson.is_object())
                                {
                                    logWarningLimited(std::format("Node at index {} is not an object, skipping", i));
                                    continue;
                                }

                                try
                                {
                                    ShaderNode node;
                                    node.id = nodeJson.value("id", 0u);

                                    // Check for duplicate node IDs
                                    if (nodeIds.count(node.id))
                                    {
                                        logWarningLimited(std::format(
                                            "Duplicate node ID {} at index {}, assigning new ID", node.id, i));
                                        node.id = material.graph.nextNodeId++;
                                    }
                                    nodeIds.insert(node.id);

                                    node.type = stringToNodeType(nodeJson.value("type", "ConstantScalar"));
                                    node.name = nodeJson.value("name", "");

                                    // Parse position with validation
                                    if (nodeJson.contains("position"))
                                    {
                                        const auto& posJson = nodeJson["position"];
                                        if (posJson.is_array() && posJson.size() >= 2 &&
                                            posJson[0].is_number() && posJson[1].is_number())
                                        {
                                            node.position.x = posJson[0].get<float>();
                                            node.position.y = posJson[1].get<float>();
                                        }
                                        else
                                        {
                                            logWarningLimited(std::format(
                                                "Node {} has invalid position format, using default", node.id));
                                            node.position = glm::vec2(100.0f * i, 100.0f);
                                        }
                                    }

                                    // Parse properties
                                    if (nodeJson.contains("properties"))
                                    {
                                        if (nodeJson["properties"].is_object())
                                        {
                                            for (auto& [key, val] : nodeJson["properties"].items())
                                            {
                                                std::string context = std::format("node {}.{}", node.id, key);
                                                node.properties[key] = deserializeProperty(val, context);
                                            }
                                        }
                                        else
                                        {
                                            logWarningLimited(std::format(
                                                "Node {} 'properties' is not an object, skipping properties", node.id));
                                        }
                                    }

                                    material.graph.nodes.push_back(std::move(node));
                                    material.graph.nextNodeId = std::max(material.graph.nextNodeId, node.id + 1);
                                }
                                catch (const std::exception& e)
                                {
                                    logWarningLimited(std::format(
                                        "Failed to parse node at index {}: {}", i, e.what()));
                                }
                            }
                        }
                    }

                    // Build maps for link validation
                    std::unordered_set<uint32_t> validNodeIds;
                    std::unordered_map<uint32_t, NodeType> nodeTypeMap;
                    for (const auto& node : material.graph.nodes)
                    {
                        validNodeIds.insert(node.id);
                        nodeTypeMap[node.id] = node.type;
                    }

                    // Helper: nodes that have NO output pins (only inputs)
                    auto isNodeTypeWithNoOutputs = [](NodeType type) -> bool
                    {
                        return type == NodeType::PBROutput;
                    };

                    // Helper: nodes that have NO input pins (only outputs)
                    auto isNodeTypeWithNoInputs = [](NodeType type) -> bool
                    {
                        return type == NodeType::VertexUV ||
                            type == NodeType::VertexNormal ||
                            type == NodeType::Time;
                    };

                    // Known output pin names for nodes that only have outputs
                    auto isOutputOnlyPin = [](NodeType type, const std::string& pin) -> bool
                    {
                        if (type == NodeType::TextureSample)
                        {
                            return pin == "RGBA" || pin == "RGB" || pin == "R" ||
                                pin == "G" || pin == "B" || pin == "A";
                        }
                        if (type == NodeType::VertexUV)
                        {
                            return pin == "UV" || pin == "U" || pin == "V";
                        }
                        if (type == NodeType::VertexNormal)
                        {
                            return pin == "Normal" || pin == "X" || pin == "Y" || pin == "Z";
                        }
                        if (type == NodeType::Time)
                        {
                            return pin == "Time";
                        }
                        return false;
                    };

                    // Known input pin names for PBROutput
                    auto isPBROutputInputPin = [](const std::string& pin) -> bool
                    {
                        return pin == "Albedo" || pin == "Metallic" || pin == "Roughness" ||
                            pin == "Normal" || pin == "AO" || pin == "Emission" ||
                            pin == "EmissionStrength" || pin == "Opacity";
                    };

                    // Parse links with validation
                    if (graphJson.contains("links"))
                    {
                        if (!graphJson["links"].is_array())
                        {
                            logWarningLimited("'graph.links' is not an array, skipping links");
                        }
                        else
                        {
                            for (size_t i = 0; i < graphJson["links"].size(); ++i)
                            {
                                const auto& linkJson = graphJson["links"][i];

                                if (!linkJson.is_object())
                                {
                                    logWarningLimited(std::format("Link at index {} is not an object, skipping", i));
                                    continue;
                                }

                                try
                                {
                                    NodeLink link;
                                    link.id = linkJson.value("id", 0u);
                                    link.sourceNodeId = linkJson.value("sourceNode", 0u);
                                    link.targetNodeId = linkJson.value("targetNode", 0u);
                                    link.sourcePin = linkJson.value("sourcePin", "");
                                    link.targetPin = linkJson.value("targetPin", "");

                                    // Validate link references existing nodes
                                    if (!validNodeIds.count(link.sourceNodeId))
                                    {
                                        logWarningLimited(std::format(
                                            "Link {} references non-existent source node {}, skipping",
                                            link.id, link.sourceNodeId));
                                        continue;
                                    }
                                    if (!validNodeIds.count(link.targetNodeId))
                                    {
                                        logWarningLimited(std::format(
                                            "Link {} references non-existent target node {}, skipping",
                                            link.id, link.targetNodeId));
                                        continue;
                                    }

                                    // Validate pin names are not empty
                                    if (link.sourcePin.empty() || link.targetPin.empty())
                                    {
                                        logWarningLimited(std::format(
                                            "Link {} has empty pin name(s), skipping", link.id));
                                        continue;
                                    }

                                    // Validate link direction - source node must have output pins
                                    NodeType sourceType = nodeTypeMap[link.sourceNodeId];
                                    NodeType targetType = nodeTypeMap[link.targetNodeId];

                                    if (isNodeTypeWithNoOutputs(sourceType))
                                    {
                                        logWarningLimited(std::format(
                                            "Link {} has invalid source: node {} ({}) has no output pins, skipping",
                                            link.id, link.sourceNodeId, nodeTypeToString(sourceType)));
                                        continue;
                                    }

                                    // Target node must have input pins
                                    if (isNodeTypeWithNoInputs(targetType))
                                    {
                                        logWarningLimited(std::format(
                                            "Link {} has invalid target: node {} ({}) has no input pins, skipping",
                                            link.id, link.targetNodeId, nodeTypeToString(targetType)));
                                        continue;
                                    }

                                    // Check if link appears to be reversed (source pin is an input, target pin is an output)
                                    bool sourceIsActuallyInput = (sourceType == NodeType::PBROutput &&
                                        isPBROutputInputPin(link.sourcePin));
                                    bool targetIsActuallyOutput = isOutputOnlyPin(targetType, link.targetPin);

                                    if (sourceIsActuallyInput || targetIsActuallyOutput)
                                    {
                                        logWarningLimited(std::format(
                                            "Link {} appears to have reversed direction (source pin '{}' or target pin '{}' has wrong kind), skipping",
                                            link.id, link.sourcePin, link.targetPin));
                                        continue;
                                    }

                                    material.graph.links.push_back(std::move(link));
                                    material.graph.nextLinkId = std::max(material.graph.nextLinkId, link.id + 1);
                                }
                                catch (const std::exception& e)
                                {
                                    logWarningLimited(std::format(
                                        "Failed to parse link at index {}: {}", i, e.what()));
                                }
                            }
                        }
                    }
                }
            }

            // Ensure PBROutput node exists (required for valid material)
            bool hasPBROutput = false;
            for (const auto& node : material.graph.nodes)
            {
                if (node.type == NodeType::PBROutput)
                {
                    hasPBROutput = true;
                    break;
                }
            }
            if (!hasPBROutput)
            {
                logWarningLimited("Material is missing PBROutput node, adding default");
                ShaderNode outputNode;
                outputNode.id = material.graph.nextNodeId++;
                outputNode.type = NodeType::PBROutput;
                outputNode.name = "PBR Output";
                outputNode.position = glm::vec2(300.0f, 200.0f);
                material.graph.nodes.push_back(std::move(outputNode));
            }

            // Parse cached shaders
            if (j.contains("cachedShader"))
            {
                if (j["cachedShader"].is_object())
                {
                    material.cachedVertexShader = j["cachedShader"].value("vertexCode", "");
                    material.cachedFragmentShader = j["cachedShader"].value("fragmentCode", "");
                    material.needsRecompile = material.cachedFragmentShader.empty();

                    // Check for outdated shaders - v1.0 used 6 textures, v1.1 uses 16 textures
                    // Force recompile if shader was compiled with old texture array size
                    if (!material.needsRecompile && !material.cachedFragmentShader.empty())
                    {
                        bool isOutdated = false;

                        // Check for old array declaration (u_Textures[6] means old format)
                        // Current v1.1 format uses u_Textures[16]
                        std::string oldDeclPattern = "u_Textures[6]";
                        if (material.cachedFragmentShader.find(oldDeclPattern) != std::string::npos)
                        {
                            isOutdated = true;
                        }

                        // Also check for any declaration that isn't the current MAX_MATERIAL_TEXTURES
                        if (!isOutdated)
                        {
                            std::string expectedDecl = "u_Textures[" + std::to_string(MAX_MATERIAL_TEXTURES) + "]";
                            // If we don't find the expected declaration, it might be outdated
                            if (material.cachedFragmentShader.find(expectedDecl) == std::string::npos)
                            {
                                // Double-check by looking for any other size
                                for (int size = 1; size < MAX_MATERIAL_TEXTURES; ++size)
                                {
                                    std::string declPattern = "u_Textures[" + std::to_string(size) + "]";
                                    if (material.cachedFragmentShader.find(declPattern) != std::string::npos)
                                    {
                                        isOutdated = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if (isOutdated || needsMigration)
                        {
                            // Force recompile for version upgrade
                            logWarningLimited(
                                "Material has outdated cached shader (texture array size changed), clearing for recompile");
                            material.needsRecompile = true;
                            material.cachedVertexShader.clear();
                            material.cachedFragmentShader.clear();
                        }
                    }
                }
                else
                {
                    logWarningLimited("'cachedShader' field is not an object, ignoring cached shaders");
                }
            }

            if (warningCount > 0)
            {
                vfLogWarning("Loaded material '{}' with {} warning(s)", material.name, warningCount);
            }
            return material;
        }
        catch (const json::exception& e)
        {
            vfLogError("Failed to parse material file '{}': {}", path, e.what());
            return std::nullopt;
        }
        catch (const std::exception& e)
        {
            vfLogError("Unexpected error loading material '{}': {}", path, e.what());
            return std::nullopt;
        }
    }

    bool MaterialAsset::save(std::string_view path, const MaterialData& material)
    {
        json j;

        j["version"] = MATERIAL_FORMAT_VERSION;
        j["uuid"] = material.uuid;
        j["name"] = material.name;
        j["blendMode"] = blendModeToString(material.blendMode);

        // Shader graph
        json graphJson;
        json nodesJson = json::array();
        for (const auto& node : material.graph.nodes)
        {
            json nodeJson;
            nodeJson["id"] = node.id;
            nodeJson["type"] = nodeTypeToString(node.type);
            nodeJson["name"] = node.name;
            nodeJson["position"] = json::array({node.position.x, node.position.y});

            if (!node.properties.empty())
            {
                json propsJson;
                for (const auto& [key, val] : node.properties)
                {
                    propsJson[key] = serializeProperty(val);
                }
                nodeJson["properties"] = propsJson;
            }

            nodesJson.push_back(nodeJson);
        }
        graphJson["nodes"] = nodesJson;

        json linksJson = json::array();
        for (const auto& link : material.graph.links)
        {
            json linkJson;
            linkJson["id"] = link.id;
            linkJson["sourceNode"] = link.sourceNodeId;
            linkJson["targetNode"] = link.targetNodeId;
            linkJson["sourcePin"] = link.sourcePin;
            linkJson["targetPin"] = link.targetPin;
            linksJson.push_back(linkJson);
        }
        graphJson["links"] = linksJson;
        j["graph"] = graphJson;

        // Cached shaders
        if (!material.cachedVertexShader.empty() || !material.cachedFragmentShader.empty())
        {
            json cachedJson;
            cachedJson["vertexCode"] = material.cachedVertexShader;
            cachedJson["fragmentCode"] = material.cachedFragmentShader;
            j["cachedShader"] = cachedJson;
        }

        // Write to file
        try
        {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to create material file: {}", path);
                return false;
            }

            file << j.dump(4); // Pretty print with 4-space indent

            // Flush to OS buffers before closing to avoid race conditions
            // where readers might see incomplete/stale data
            file.flush();
            if (!file.good())
            {
                vfLogError("Failed to flush material file: {}", path);
                return false;
            }

            file.close();
            if (file.fail())
            {
                vfLogError("Failed to close material file: {}", path);
                return false;
            }

            vfLogInfo("Saved material: {} to {}", material.name, path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save material file {}: {}", path, e.what());
            return false;
        }
    }

    MaterialData MaterialAsset::createDefault(const std::string& name)
    {
        MaterialData material;
        material.uuid = std::to_string(uuid::UUID().getValue());
        material.name = name;
        material.blendMode = BlendMode::Opaque;
        material.needsRecompile = true;

        // Create only the PBR Output node - user will add other nodes
        ShaderNode outputNode;
        outputNode.id = material.graph.nextNodeId++;
        outputNode.type = NodeType::PBROutput;
        outputNode.name = "PBR Output";
        outputNode.position = glm::vec2(300.0f, 200.0f);

        material.graph.nodes.push_back(std::move(outputNode));

        return material;
    }
}
