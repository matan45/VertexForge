#include "MaterialAsset.hpp"
#include "../print/EditorLogger.hpp"
#include "../uuid/UUID.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <functional>
#include <format>

namespace material
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;
    using LogWarningFn = std::function<void(const std::string&)>;

    namespace
    {
        bool isNodeTypeWithNoOutputs(NodeType type) { return type == NodeType::PBROutput; }

        bool isNodeTypeWithNoInputs(NodeType type)
        {
            return type == NodeType::VertexUV || type == NodeType::VertexNormal || type == NodeType::Time;
        }

        bool isOutputOnlyPin(NodeType type, const std::string& pin)
        {
            if (type == NodeType::TextureSample)
                return pin == "RGBA" || pin == "RGB" || pin == "R" || pin == "G" || pin == "B" || pin == "A";
            if (type == NodeType::VertexUV)
                return pin == "UV" || pin == "U" || pin == "V";
            if (type == NodeType::VertexNormal)
                return pin == "Normal" || pin == "X" || pin == "Y" || pin == "Z";
            if (type == NodeType::Time)
                return pin == "Time";
            return false;
        }

        bool isPBROutputInputPin(const std::string& pin)
        {
            return pin == "Albedo" || pin == "Metallic" || pin == "Roughness" ||
                pin == "Normal" || pin == "AO" || pin == "Emission" ||
                pin == "EmissionStrength" || pin == "Opacity";
        }

        void parseNodePosition(
            const json& nodeJson, ShaderNode& node,
            size_t index, const LogWarningFn& logWarning)
        {
            if (!nodeJson.contains("position"))
                return;
            const auto& posJson = nodeJson["position"];
            if (posJson.is_array() && posJson.size() >= 2 &&
                posJson[0].is_number() && posJson[1].is_number())
            {
                node.position.x = posJson[0].get<float>();
                node.position.y = posJson[1].get<float>();
            }
            else
            {
                logWarning(std::format("Node {} has invalid position format, using default", node.id));
                node.position = glm::vec2(100.0f * index, 100.0f);
            }
        }

        void parseNodeProperties(
            const json& nodeJson, ShaderNode& node,
            const LogWarningFn& logWarning)
        {
            if (!nodeJson.contains("properties"))
                return;
            if (nodeJson["properties"].is_object())
            {
                for (auto& [key, val] : nodeJson["properties"].items())
                {
                    std::string context = std::format("node {}.{}", node.id, key);
                    node.properties[key] = MaterialAsset::deserializeProperty(val, context);
                }
            }
            else
            {
                logWarning(std::format(
                    "Node {} 'properties' is not an object, skipping properties", node.id));
            }
        }

        ShaderNode parseSingleNode(
            const json& nodeJson, size_t index,
            std::unordered_set<uint32_t>& nodeIds, MaterialData& material,
            std::unordered_map<uint32_t, uint32_t>& nodeIdRemap,
            const LogWarningFn& logWarning)
        {
            ShaderNode node;
            uint32_t originalId = nodeJson.value("id", 0u);
            node.id = originalId;

            if (nodeIds.count(node.id))
            {
                node.id = material.graph.nextNodeId++;
                nodeIdRemap[originalId] = node.id;
                logWarning(std::format(
                    "Duplicate node ID {} at index {}, remapped to {}", originalId, index, node.id));
            }
            nodeIds.insert(node.id);

            node.type = stringToNodeType(nodeJson.value("type", "ConstantScalar"));
            node.name = nodeJson.value("name", "");
            parseNodePosition(nodeJson, node, index, logWarning);
            parseNodeProperties(nodeJson, node, logWarning);
            return node;
        }

        void parseGraphNodes(
            const json& graphJson, MaterialData& material,
            std::unordered_map<uint32_t, uint32_t>& nodeIdRemap,
            const LogWarningFn& logWarning)
        {
            if (!graphJson.contains("nodes"))
                return;
            if (!graphJson["nodes"].is_array())
            {
                logWarning("'graph.nodes' is not an array, skipping nodes");
                return;
            }

            std::unordered_set<uint32_t> nodeIds;
            for (size_t i = 0; i < graphJson["nodes"].size(); ++i)
            {
                const auto& nodeJson = graphJson["nodes"][i];
                if (!nodeJson.is_object())
                {
                    logWarning(std::format("Node at index {} is not an object, skipping", i));
                    continue;
                }
                try
                {
                    ShaderNode node = parseSingleNode(
                        nodeJson, i, nodeIds, material, nodeIdRemap, logWarning);
                    material.graph.nextNodeId = std::max(material.graph.nextNodeId, node.id + 1);
                    material.graph.nodes.push_back(std::move(node));
                }
                catch (const std::exception& e)
                {
                    logWarning(std::format("Failed to parse node at index {}: {}", i, e.what()));
                }
            }
        }

        bool validateLinkEndpoints(
            const NodeLink& link,
            const std::unordered_set<uint32_t>& validNodeIds,
            const LogWarningFn& logWarning)
        {
            if (!validNodeIds.count(link.sourceNodeId))
            {
                logWarning(std::format(
                    "Link {} references non-existent source node {}, skipping",
                    link.id, link.sourceNodeId));
                return false;
            }
            if (!validNodeIds.count(link.targetNodeId))
            {
                logWarning(std::format(
                    "Link {} references non-existent target node {}, skipping",
                    link.id, link.targetNodeId));
                return false;
            }
            if (link.sourcePin.empty() || link.targetPin.empty())
            {
                logWarning(std::format("Link {} has empty pin name(s), skipping", link.id));
                return false;
            }
            return true;
        }

        bool validateLinkDirection(
            const NodeLink& link,
            const std::unordered_map<uint32_t, NodeType>& nodeTypeMap,
            const LogWarningFn& logWarning)
        {
            NodeType sourceType = nodeTypeMap.at(link.sourceNodeId);
            NodeType targetType = nodeTypeMap.at(link.targetNodeId);

            if (isNodeTypeWithNoOutputs(sourceType))
            {
                logWarning(std::format(
                    "Link {} has invalid source: node {} ({}) has no output pins, skipping",
                    link.id, link.sourceNodeId, nodeTypeToString(sourceType)));
                return false;
            }
            if (isNodeTypeWithNoInputs(targetType))
            {
                logWarning(std::format(
                    "Link {} has invalid target: node {} ({}) has no input pins, skipping",
                    link.id, link.targetNodeId, nodeTypeToString(targetType)));
                return false;
            }

            bool sourceIsActuallyInput = (sourceType == NodeType::PBROutput &&
                isPBROutputInputPin(link.sourcePin));
            bool targetIsActuallyOutput = isOutputOnlyPin(targetType, link.targetPin);
            if (sourceIsActuallyInput || targetIsActuallyOutput)
            {
                logWarning(std::format(
                    "Link {} appears to have reversed direction (source pin '{}' or target pin '{}' has wrong kind), skipping",
                    link.id, link.sourcePin, link.targetPin));
                return false;
            }
            return true;
        }

        void parseSingleLink(
            const json& linkJson, MaterialData& material,
            const std::unordered_map<uint32_t, uint32_t>& nodeIdRemap,
            const std::unordered_set<uint32_t>& validNodeIds,
            const std::unordered_map<uint32_t, NodeType>& nodeTypeMap,
            const LogWarningFn& logWarning)
        {
            NodeLink link;
            link.id = linkJson.value("id", 0u);
            auto remap = [&](uint32_t id) { auto it = nodeIdRemap.find(id); return it != nodeIdRemap.end() ? it->second : id; };
            link.sourceNodeId = remap(linkJson.value("sourceNode", 0u));
            link.targetNodeId = remap(linkJson.value("targetNode", 0u));
            link.sourcePin = linkJson.value("sourcePin", "");
            link.targetPin = linkJson.value("targetPin", "");

            if (!validateLinkEndpoints(link, validNodeIds, logWarning))
                return;
            if (!validateLinkDirection(link, nodeTypeMap, logWarning))
                return;

            material.graph.nextLinkId = std::max(material.graph.nextLinkId, link.id + 1);
            material.graph.links.push_back(std::move(link));
        }

        void parseGraphLinks(
            const json& graphJson, MaterialData& material,
            const std::unordered_map<uint32_t, uint32_t>& nodeIdRemap,
            const LogWarningFn& logWarning)
        {
            if (!graphJson.contains("links"))
                return;
            if (!graphJson["links"].is_array())
            {
                logWarning("'graph.links' is not an array, skipping links");
                return;
            }

            std::unordered_set<uint32_t> validNodeIds;
            std::unordered_map<uint32_t, NodeType> nodeTypeMap;
            for (const auto& node : material.graph.nodes)
            {
                validNodeIds.insert(node.id);
                nodeTypeMap[node.id] = node.type;
            }

            for (size_t i = 0; i < graphJson["links"].size(); ++i)
            {
                const auto& linkJson = graphJson["links"][i];
                if (!linkJson.is_object())
                {
                    logWarning(std::format("Link at index {} is not an object, skipping", i));
                    continue;
                }
                try
                {
                    parseSingleLink(linkJson, material, nodeIdRemap, validNodeIds, nodeTypeMap, logWarning);
                }
                catch (const std::exception& e)
                {
                    logWarning(std::format("Failed to parse link at index {}: {}", i, e.what()));
                }
            }
        }

        void ensurePBROutputNode(MaterialData& material, const LogWarningFn& logWarning)
        {
            for (const auto& node : material.graph.nodes)
            {
                if (node.type == NodeType::PBROutput)
                    return;
            }
            logWarning("Material is missing PBROutput node, adding default");
            ShaderNode outputNode;
            outputNode.id = material.graph.nextNodeId++;
            outputNode.type = NodeType::PBROutput;
            outputNode.name = "PBR Output";
            outputNode.position = glm::vec2(300.0f, 200.0f);
            material.graph.nodes.push_back(std::move(outputNode));
        }

        bool isShaderTextureArrayOutdated(const std::string& fragmentShader)
        {
            if (fragmentShader.find("u_Textures[6]") != std::string::npos)
                return true;
            std::string expectedDecl = "u_Textures[" + std::to_string(MAX_MATERIAL_TEXTURES) + "]";
            if (fragmentShader.find(expectedDecl) == std::string::npos)
            {
                for (int size = 1; size < MAX_MATERIAL_TEXTURES; ++size)
                {
                    std::string declPattern = "u_Textures[" + std::to_string(size) + "]";
                    if (fragmentShader.find(declPattern) != std::string::npos)
                        return true;
                }
            }
            return false;
        }

        void parseCachedShaders(
            const json& j, MaterialData& material,
            bool needsMigration, const LogWarningFn& logWarning)
        {
            if (!j.contains("cachedShader"))
                return;
            if (!j["cachedShader"].is_object())
            {
                logWarning("'cachedShader' field is not an object, ignoring cached shaders");
                return;
            }

            material.cachedVertexShader = j["cachedShader"].value("vertexCode", "");
            material.cachedFragmentShader = j["cachedShader"].value("fragmentCode", "");
            material.needsRecompile = material.cachedFragmentShader.empty();

            if (material.needsRecompile || material.cachedFragmentShader.empty())
                return;

            if (isShaderTextureArrayOutdated(material.cachedFragmentShader) || needsMigration)
            {
                logWarning(
                    "Material has outdated cached shader (texture array size changed), clearing for recompile");
                material.needsRecompile = true;
                material.cachedVertexShader.clear();
                material.cachedFragmentShader.clear();
            }
        }

        std::optional<json> readAndValidateFile(std::string_view path)
        {
            fs::path filePath(path);
            if (!fs::exists(filePath))
            {
                vfLogError("Material file not found: {}", path);
                return std::nullopt;
            }

            std::error_code ec;
            auto fileSize = fs::file_size(filePath, ec);
            if (ec)
            {
                vfLogError("Cannot read material file size '{}': {}", path, ec.message());
                return std::nullopt;
            }
            constexpr size_t MAX_MATERIAL_FILE_SIZE = 10 * 1024 * 1024;
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

            json j;
            try { file >> j; }
            catch (const json::parse_error& e)
            {
                vfLogError("Material file '{}' contains invalid JSON at byte {}: {}",
                           path, e.byte, e.what());
                return std::nullopt;
            }

            if (!j.is_object())
            {
                vfLogError("Material file '{}' must contain a JSON object at root level", path);
                return std::nullopt;
            }
            return j;
        }

        struct WarningTracker
        {
            int count = 0;
            static constexpr int MAX_WARNINGS = 20;

            LogWarningFn makeLogger()
            {
                return [this](const std::string& msg)
                {
                    if (count < MAX_WARNINGS)
                    {
                        vfLogWarning("{}", msg);
                        if (++count == MAX_WARNINGS)
                            vfLogWarning("(suppressing further warnings for this file)");
                    }
                };
            }
        };

        void parseBasicFields(
            const json& j, MaterialData& material,
            std::string_view path, bool& needsMigration,
            const LogWarningFn& logWarning)
        {
            std::string fileVersion = j.value("version", MATERIAL_FORMAT_VERSION);
            needsMigration = (fileVersion != MATERIAL_FORMAT_VERSION);
            if (needsMigration)
            {
                logWarning(std::format(
                    "Material file '{}' has version {} (current is {}). Will migrate on save.",
                    std::string(path), fileVersion, MATERIAL_FORMAT_VERSION));
            }

            material.uuid = j.value("uuid", std::to_string(uuid::UUID().getValue()));
            material.name = j.value("name", "Unnamed Material");
            if (material.name.empty())
            {
                material.name = "Unnamed Material";
                logWarning("Material has empty name, using default");
            }
            material.blendMode = stringToBlendMode(j.value("blendMode", "opaque"));
        }

        void parseGraph(
            const json& j, MaterialData& material,
            const LogWarningFn& logWarning)
        {
            if (j.contains("graph") && j["graph"].is_object())
            {
                const auto& graphJson = j["graph"];
                std::unordered_map<uint32_t, uint32_t> nodeIdRemap;
                parseGraphNodes(graphJson, material, nodeIdRemap, logWarning);
                parseGraphLinks(graphJson, material, nodeIdRemap, logWarning);
            }
            else if (j.contains("graph"))
            {
                logWarning("'graph' field is not an object, skipping graph data");
            }
        }

        std::optional<MaterialData> parseMaterialData(const json& j, std::string_view path)
        {
            WarningTracker warnings;
            auto logWarning = warnings.makeLogger();

            try
            {
                MaterialData material;
                bool needsMigration = false;

                parseBasicFields(j, material, path, needsMigration, logWarning);
                parseGraph(j, material, logWarning);
                ensurePBROutputNode(material, logWarning);
                parseCachedShaders(j, material, needsMigration, logWarning);

                if (warnings.count > 0)
                    vfLogWarning("Loaded material '{}' with {} warning(s)", material.name, warnings.count);
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
    } // anonymous namespace

    std::optional<MaterialData> MaterialAsset::load(std::string_view path)
    {
        auto j = readAndValidateFile(path);
        if (!j)
            return std::nullopt;
        return parseMaterialData(*j, path);
    }
}
