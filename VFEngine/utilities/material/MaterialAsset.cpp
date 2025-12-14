#include "MaterialAsset.hpp"
#include "../print/EditorLogger.hpp"
#include "../uuid/UUID.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <format>

namespace material {

    using json = nlohmann::json;
    namespace fs = std::filesystem;

    // Helper: Convert NodeType to string
    static std::string nodeTypeToString(NodeType type) {
        switch (type) {
            case NodeType::PBROutput: return "PBROutput";
            case NodeType::ConstantScalar: return "ConstantScalar";
            case NodeType::ConstantVec2: return "ConstantVec2";
            case NodeType::ConstantVec3: return "ConstantVec3";
            case NodeType::ConstantColor: return "ConstantColor";
            case NodeType::Add: return "Add";
            case NodeType::Subtract: return "Subtract";
            case NodeType::Multiply: return "Multiply";
            case NodeType::Divide: return "Divide";
            case NodeType::Power: return "Power";
            case NodeType::Lerp: return "Lerp";
            case NodeType::Clamp: return "Clamp";
            case NodeType::Saturate: return "Saturate";
            case NodeType::OneMinus: return "OneMinus";
            case NodeType::Abs: return "Abs";
            case NodeType::Floor: return "Floor";
            case NodeType::Ceil: return "Ceil";
            case NodeType::Fract: return "Fract";
            case NodeType::Sin: return "Sin";
            case NodeType::Cos: return "Cos";
            case NodeType::Dot: return "Dot";
            case NodeType::Cross: return "Cross";
            case NodeType::Normalize: return "Normalize";
            case NodeType::Length: return "Length";
            case NodeType::MakeVec2: return "MakeVec2";
            case NodeType::MakeVec3: return "MakeVec3";
            case NodeType::MakeVec4: return "MakeVec4";
            case NodeType::SplitVec2: return "SplitVec2";
            case NodeType::SplitVec3: return "SplitVec3";
            case NodeType::SplitVec4: return "SplitVec4";
            case NodeType::Fresnel: return "Fresnel";
            case NodeType::VertexNormal: return "VertexNormal";
            case NodeType::VertexUV: return "VertexUV";
            case NodeType::Time: return "Time";
            case NodeType::TextureSample: return "TextureSample";
            default: return "Unknown";
        }
    }

    // Helper: Convert string to NodeType
    static NodeType stringToNodeType(const std::string& str) {
        if (str == "PBROutput") return NodeType::PBROutput;
        if (str == "ConstantScalar") return NodeType::ConstantScalar;
        if (str == "ConstantVec2") return NodeType::ConstantVec2;
        if (str == "ConstantVec3") return NodeType::ConstantVec3;
        if (str == "ConstantColor") return NodeType::ConstantColor;
        if (str == "Add") return NodeType::Add;
        if (str == "Subtract") return NodeType::Subtract;
        if (str == "Multiply") return NodeType::Multiply;
        if (str == "Divide") return NodeType::Divide;
        if (str == "Power") return NodeType::Power;
        if (str == "Lerp") return NodeType::Lerp;
        if (str == "Clamp") return NodeType::Clamp;
        if (str == "Saturate") return NodeType::Saturate;
        if (str == "OneMinus") return NodeType::OneMinus;
        if (str == "Abs") return NodeType::Abs;
        if (str == "Floor") return NodeType::Floor;
        if (str == "Ceil") return NodeType::Ceil;
        if (str == "Fract") return NodeType::Fract;
        if (str == "Sin") return NodeType::Sin;
        if (str == "Cos") return NodeType::Cos;
        if (str == "Dot") return NodeType::Dot;
        if (str == "Cross") return NodeType::Cross;
        if (str == "Normalize") return NodeType::Normalize;
        if (str == "Length") return NodeType::Length;
        if (str == "MakeVec2") return NodeType::MakeVec2;
        if (str == "MakeVec3") return NodeType::MakeVec3;
        if (str == "MakeVec4") return NodeType::MakeVec4;
        if (str == "SplitVec2") return NodeType::SplitVec2;
        if (str == "SplitVec3") return NodeType::SplitVec3;
        if (str == "SplitVec4") return NodeType::SplitVec4;
        if (str == "Fresnel") return NodeType::Fresnel;
        if (str == "VertexNormal") return NodeType::VertexNormal;
        if (str == "VertexUV") return NodeType::VertexUV;
        if (str == "Time") return NodeType::Time;
        if (str == "TextureSample") return NodeType::TextureSample;
        return NodeType::ConstantScalar;  // Default
    }

    // Helper: Convert BlendMode to string
    static std::string blendModeToString(BlendMode mode) {
        switch (mode) {
            case BlendMode::Opaque: return "opaque";
            case BlendMode::Masked: return "masked";
            case BlendMode::Translucent: return "translucent";
            default: return "opaque";
        }
    }

    // Helper: Convert string to BlendMode
    static BlendMode stringToBlendMode(const std::string& str) {
        if (str == "masked") return BlendMode::Masked;
        if (str == "translucent") return BlendMode::Translucent;
        return BlendMode::Opaque;
    }

    // Helper: Convert ParameterType to string
    static std::string paramTypeToString(ParameterType type) {
        switch (type) {
            case ParameterType::Scalar: return "scalar";
            case ParameterType::Vec2: return "vec2";
            case ParameterType::Vec3: return "vec3";
            case ParameterType::Vec4: return "vec4";
            case ParameterType::Color: return "color";
            default: return "scalar";
        }
    }

    // Helper: Convert string to ParameterType
    static ParameterType stringToParamType(const std::string& str) {
        if (str == "vec2") return ParameterType::Vec2;
        if (str == "vec3") return ParameterType::Vec3;
        if (str == "vec4") return ParameterType::Vec4;
        if (str == "color") return ParameterType::Color;
        return ParameterType::Scalar;
    }

    // Serialize node property to JSON
    static json serializeProperty(const NodeProperty& prop) {
        return std::visit([](auto&& arg) -> json {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, float>) {
                return arg;
            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                return json::array({ arg.x, arg.y });
            } else if constexpr (std::is_same_v<T, glm::vec3>) {
                return json::array({ arg.x, arg.y, arg.z });
            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                return json::array({ arg.x, arg.y, arg.z, arg.w });
            } else if constexpr (std::is_same_v<T, std::string>) {
                return arg;
            }
            return json();
        }, prop);
    }

    // Deserialize node property from JSON with validation
    static NodeProperty deserializeProperty(const json& j, const std::string& context = "") {
        try {
            if (j.is_number()) {
                return j.get<float>();
            } else if (j.is_string()) {
                return j.get<std::string>();
            } else if (j.is_array()) {
                // Validate array elements are numbers
                for (size_t i = 0; i < j.size(); ++i) {
                    if (!j[i].is_number()) {
                        vfLogWarning("Invalid array element at index {} in property{}, using default",
                                    i, context.empty() ? "" : " '" + context + "'");
                        return 0.0f;
                    }
                }
                if (j.size() == 2) {
                    return glm::vec2(j[0].get<float>(), j[1].get<float>());
                } else if (j.size() == 3) {
                    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
                } else if (j.size() == 4) {
                    return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
                } else {
                    vfLogWarning("Unexpected array size {} in property{}, using default",
                                j.size(), context.empty() ? "" : " '" + context + "'");
                }
            } else if (!j.is_null()) {
                vfLogWarning("Unexpected JSON type for property{}, using default",
                            context.empty() ? "" : " '" + context + "'");
            }
        } catch (const json::exception& e) {
            vfLogWarning("Failed to parse property{}: {}",
                        context.empty() ? "" : " '" + context + "'", e.what());
        }
        return 0.0f;  // Default
    }

    // Serialize parameter value to JSON
    static json serializeParamValue(const ParameterValue& val) {
        return std::visit([](auto&& arg) -> json {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, float>) {
                return arg;
            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                return json::array({ arg.x, arg.y });
            } else if constexpr (std::is_same_v<T, glm::vec3>) {
                return json::array({ arg.x, arg.y, arg.z });
            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                return json::array({ arg.x, arg.y, arg.z, arg.w });
            }
            return json();
        }, val);
    }

    // Deserialize parameter value from JSON based on type with validation
    static ParameterValue deserializeParamValue(const json& j, ParameterType type, const std::string& paramName = "") {
        try {
            switch (type) {
                case ParameterType::Scalar:
                    if (!j.is_number()) {
                        vfLogWarning("Parameter '{}' expected scalar, got different type", paramName);
                        return 0.0f;
                    }
                    return j.get<float>();
                case ParameterType::Vec2:
                    if (!j.is_array() || j.size() < 2) {
                        vfLogWarning("Parameter '{}' expected vec2 array, using default", paramName);
                        return glm::vec2(0.0f);
                    }
                    return glm::vec2(j[0].get<float>(), j[1].get<float>());
                case ParameterType::Vec3:
                    if (!j.is_array() || j.size() < 3) {
                        vfLogWarning("Parameter '{}' expected vec3 array, using default", paramName);
                        return glm::vec3(0.0f);
                    }
                    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
                case ParameterType::Vec4:
                case ParameterType::Color:
                    if (!j.is_array() || j.size() < 4) {
                        vfLogWarning("Parameter '{}' expected vec4 array, using default", paramName);
                        return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                    }
                    return glm::vec4(j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>());
                default:
                    return 0.0f;
            }
        } catch (const json::exception& e) {
            vfLogWarning("Failed to parse parameter '{}': {}", paramName, e.what());
            // Return type-appropriate default
            switch (type) {
                case ParameterType::Vec2: return glm::vec2(0.0f);
                case ParameterType::Vec3: return glm::vec3(0.0f);
                case ParameterType::Vec4:
                case ParameterType::Color: return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                default: return 0.0f;
            }
        }
    }

    std::optional<MaterialData> MaterialAsset::load(std::string_view path) {
        fs::path filePath(path);

        // Validate file exists
        if (!fs::exists(filePath)) {
            vfLogError("Material file not found: {}", path);
            return std::nullopt;
        }

        // Check file size (sanity check - materials shouldn't be huge)
        std::error_code ec;
        auto fileSize = fs::file_size(filePath, ec);
        if (ec) {
            vfLogError("Cannot read material file size '{}': {}", path, ec.message());
            return std::nullopt;
        }
        constexpr size_t MAX_MATERIAL_FILE_SIZE = 10 * 1024 * 1024;  // 10 MB limit
        if (fileSize > MAX_MATERIAL_FILE_SIZE) {
            vfLogError("Material file '{}' is too large ({} bytes, max {} bytes)",
                      path, fileSize, MAX_MATERIAL_FILE_SIZE);
            return std::nullopt;
        }

        std::ifstream file(filePath);
        if (!file.is_open()) {
            vfLogError("Failed to open material file: {}", path);
            return std::nullopt;
        }

        // Parse JSON with detailed error handling
        json j;
        try {
            file >> j;
        } catch (const json::parse_error& e) {
            vfLogError("Material file '{}' contains invalid JSON at byte {}: {}",
                      path, e.byte, e.what());
            return std::nullopt;
        }

        // Validate root is an object
        if (!j.is_object()) {
            vfLogError("Material file '{}' must contain a JSON object at root level", path);
            return std::nullopt;
        }

        MaterialData material;
        int warningCount = 0;
        constexpr int MAX_WARNINGS = 20;  // Limit warning spam

        auto logWarningLimited = [&](const std::string& msg) {
            if (warningCount < MAX_WARNINGS) {
                vfLogWarning("{}", msg);
                warningCount++;
                if (warningCount == MAX_WARNINGS) {
                    vfLogWarning("(suppressing further warnings for this file)");
                }
            }
        };

        try {
            // Check version compatibility
            std::string fileVersion = j.value("version", "1.0");
            if (fileVersion != FORMAT_VERSION) {
                logWarningLimited(std::format(
                    "Material file '{}' has version {} (expected {}). Some features may not load correctly.",
                    std::string(path), fileVersion, FORMAT_VERSION));
            }

            // Basic properties with validation
            material.uuid = j.value("uuid", std::to_string(uuid::UUID().getValue()));
            material.name = j.value("name", "Unnamed Material");

            if (material.name.empty()) {
                material.name = "Unnamed Material";
                logWarningLimited("Material has empty name, using default");
            }

            material.blendMode = stringToBlendMode(j.value("blendMode", "opaque"));

            // Shader graph with comprehensive validation
            if (j.contains("graph")) {
                if (!j["graph"].is_object()) {
                    logWarningLimited("'graph' field is not an object, skipping graph data");
                } else {
                    const auto& graphJson = j["graph"];

                    // Parse nodes
                    if (graphJson.contains("nodes")) {
                        if (!graphJson["nodes"].is_array()) {
                            logWarningLimited("'graph.nodes' is not an array, skipping nodes");
                        } else {
                            std::unordered_set<uint32_t> nodeIds;  // Track for duplicate detection

                            for (size_t i = 0; i < graphJson["nodes"].size(); ++i) {
                                const auto& nodeJson = graphJson["nodes"][i];

                                if (!nodeJson.is_object()) {
                                    logWarningLimited(std::format("Node at index {} is not an object, skipping", i));
                                    continue;
                                }

                                try {
                                    ShaderNode node;
                                    node.id = nodeJson.value("id", 0u);

                                    // Check for duplicate node IDs
                                    if (nodeIds.count(node.id)) {
                                        logWarningLimited(std::format(
                                            "Duplicate node ID {} at index {}, assigning new ID", node.id, i));
                                        node.id = material.graph.nextNodeId++;
                                    }
                                    nodeIds.insert(node.id);

                                    node.type = stringToNodeType(nodeJson.value("type", "ConstantScalar"));
                                    node.name = nodeJson.value("name", "");

                                    // Parse position with validation
                                    if (nodeJson.contains("position")) {
                                        const auto& posJson = nodeJson["position"];
                                        if (posJson.is_array() && posJson.size() >= 2 &&
                                            posJson[0].is_number() && posJson[1].is_number()) {
                                            node.position.x = posJson[0].get<float>();
                                            node.position.y = posJson[1].get<float>();
                                        } else {
                                            logWarningLimited(std::format(
                                                "Node {} has invalid position format, using default", node.id));
                                            node.position = glm::vec2(100.0f * i, 100.0f);
                                        }
                                    }

                                    // Parse properties
                                    if (nodeJson.contains("properties")) {
                                        if (nodeJson["properties"].is_object()) {
                                            for (auto& [key, val] : nodeJson["properties"].items()) {
                                                std::string context = std::format("node {}.{}", node.id, key);
                                                node.properties[key] = deserializeProperty(val, context);
                                            }
                                        } else {
                                            logWarningLimited(std::format(
                                                "Node {} 'properties' is not an object, skipping properties", node.id));
                                        }
                                    }

                                    material.graph.nodes.push_back(std::move(node));
                                    material.graph.nextNodeId = std::max(material.graph.nextNodeId, node.id + 1);

                                } catch (const std::exception& e) {
                                    logWarningLimited(std::format(
                                        "Failed to parse node at index {}: {}", i, e.what()));
                                }
                            }
                        }
                    }

                    // Build set of valid node IDs for link validation
                    std::unordered_set<uint32_t> validNodeIds;
                    for (const auto& node : material.graph.nodes) {
                        validNodeIds.insert(node.id);
                    }

                    // Parse links with validation
                    if (graphJson.contains("links")) {
                        if (!graphJson["links"].is_array()) {
                            logWarningLimited("'graph.links' is not an array, skipping links");
                        } else {
                            for (size_t i = 0; i < graphJson["links"].size(); ++i) {
                                const auto& linkJson = graphJson["links"][i];

                                if (!linkJson.is_object()) {
                                    logWarningLimited(std::format("Link at index {} is not an object, skipping", i));
                                    continue;
                                }

                                try {
                                    NodeLink link;
                                    link.id = linkJson.value("id", 0u);
                                    link.sourceNodeId = linkJson.value("sourceNode", 0u);
                                    link.targetNodeId = linkJson.value("targetNode", 0u);
                                    link.sourcePin = linkJson.value("sourcePin", "");
                                    link.targetPin = linkJson.value("targetPin", "");

                                    // Validate link references existing nodes
                                    if (!validNodeIds.count(link.sourceNodeId)) {
                                        logWarningLimited(std::format(
                                            "Link {} references non-existent source node {}, skipping",
                                            link.id, link.sourceNodeId));
                                        continue;
                                    }
                                    if (!validNodeIds.count(link.targetNodeId)) {
                                        logWarningLimited(std::format(
                                            "Link {} references non-existent target node {}, skipping",
                                            link.id, link.targetNodeId));
                                        continue;
                                    }

                                    // Validate pin names are not empty
                                    if (link.sourcePin.empty() || link.targetPin.empty()) {
                                        logWarningLimited(std::format(
                                            "Link {} has empty pin name(s), skipping", link.id));
                                        continue;
                                    }

                                    material.graph.links.push_back(std::move(link));
                                    material.graph.nextLinkId = std::max(material.graph.nextLinkId, link.id + 1);

                                } catch (const std::exception& e) {
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
            for (const auto& node : material.graph.nodes) {
                if (node.type == NodeType::PBROutput) {
                    hasPBROutput = true;
                    break;
                }
            }
            if (!hasPBROutput) {
                logWarningLimited("Material is missing PBROutput node, adding default");
                ShaderNode outputNode;
                outputNode.id = material.graph.nextNodeId++;
                outputNode.type = NodeType::PBROutput;
                outputNode.name = "PBR Output";
                outputNode.position = glm::vec2(300.0f, 200.0f);
                material.graph.nodes.push_back(std::move(outputNode));
            }

            // Parse parameters
            if (j.contains("parameters")) {
                if (!j["parameters"].is_object()) {
                    logWarningLimited("'parameters' field is not an object, skipping parameters");
                } else {
                    for (auto& [name, paramJson] : j["parameters"].items()) {
                        if (!paramJson.is_object()) {
                            logWarningLimited(std::format("Parameter '{}' is not an object, skipping", name));
                            continue;
                        }

                        try {
                            MaterialParameter param;
                            param.name = name;
                            param.type = stringToParamType(paramJson.value("type", "scalar"));
                            param.min = paramJson.value("min", 0.0f);
                            param.max = paramJson.value("max", 1.0f);

                            if (paramJson.contains("value")) {
                                param.value = deserializeParamValue(paramJson["value"], param.type, name);
                            }

                            material.parameters[name] = std::move(param);
                        } catch (const std::exception& e) {
                            logWarningLimited(std::format(
                                "Failed to parse parameter '{}': {}", name, e.what()));
                        }
                    }
                }
            }

            // Parse cached shaders
            if (j.contains("cachedShader")) {
                if (j["cachedShader"].is_object()) {
                    material.cachedVertexShader = j["cachedShader"].value("vertexCode", "");
                    material.cachedFragmentShader = j["cachedShader"].value("fragmentCode", "");
                    material.needsRecompile = material.cachedFragmentShader.empty();
                } else {
                    logWarningLimited("'cachedShader' field is not an object, ignoring cached shaders");
                }
            }

            if (warningCount > 0) {
                vfLogWarning("Loaded material '{}' with {} warning(s)", material.name, warningCount);
            }
            vfLogInfo("Loaded material: {} from {}", material.name, path);
            return material;

        } catch (const json::exception& e) {
            vfLogError("Failed to parse material file '{}': {}", path, e.what());
            return std::nullopt;
        } catch (const std::exception& e) {
            vfLogError("Unexpected error loading material '{}': {}", path, e.what());
            return std::nullopt;
        }
    }

    bool MaterialAsset::save(std::string_view path, const MaterialData& material) {
        json j;

        // Basic properties
        j["version"] = FORMAT_VERSION;
        j["uuid"] = material.uuid;
        j["name"] = material.name;
        j["blendMode"] = blendModeToString(material.blendMode);

        // Shader graph
        json graphJson;
        json nodesJson = json::array();
        for (const auto& node : material.graph.nodes) {
            json nodeJson;
            nodeJson["id"] = node.id;
            nodeJson["type"] = nodeTypeToString(node.type);
            nodeJson["name"] = node.name;
            nodeJson["position"] = json::array({ node.position.x, node.position.y });

            if (!node.properties.empty()) {
                json propsJson;
                for (const auto& [key, val] : node.properties) {
                    propsJson[key] = serializeProperty(val);
                }
                nodeJson["properties"] = propsJson;
            }

            nodesJson.push_back(nodeJson);
        }
        graphJson["nodes"] = nodesJson;

        json linksJson = json::array();
        for (const auto& link : material.graph.links) {
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

        // Parameters
        json paramsJson;
        for (const auto& [name, param] : material.parameters) {
            json paramJson;
            paramJson["type"] = paramTypeToString(param.type);
            paramJson["value"] = serializeParamValue(param.value);
            paramJson["min"] = param.min;
            paramJson["max"] = param.max;
            paramsJson[name] = paramJson;
        }
        j["parameters"] = paramsJson;

        // Cached shaders
        if (!material.cachedVertexShader.empty() || !material.cachedFragmentShader.empty()) {
            json cachedJson;
            cachedJson["vertexCode"] = material.cachedVertexShader;
            cachedJson["fragmentCode"] = material.cachedFragmentShader;
            j["cachedShader"] = cachedJson;
        }

        // Write to file
        try {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath);
            if (!file.is_open()) {
                vfLogError("Failed to create material file: {}", path);
                return false;
            }

            file << j.dump(4);  // Pretty print with 4-space indent
            vfLogInfo("Saved material: {} to {}", material.name, path);
            return true;

        } catch (const std::exception& e) {
            vfLogError("Failed to save material file {}: {}", path, e.what());
            return false;
        }
    }

    MaterialData MaterialAsset::createDefault(const std::string& name) {
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
