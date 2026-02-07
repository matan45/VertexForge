#include "TerrainMaterialAsset.hpp"
#include "../print/EditorLogger.hpp"
#include "../uuid/UUID.hpp"
#include "../material/MaterialAsset.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <format>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>

namespace terrain
{
    using json = nlohmann::json;
    namespace fs = std::filesystem;

    std::optional<TerrainMaterialData> TerrainMaterialAsset::load(std::string_view path)
    {
        fs::path filePath(path);

        if (!fs::exists(filePath))
        {
            vfLogError("Terrain material file not found: {}", path);
            return std::nullopt;
        }

        std::error_code ec;
        auto fileSize = fs::file_size(filePath, ec);
        if (ec)
        {
            vfLogError("Cannot read terrain material file size '{}': {}", path, ec.message());
            return std::nullopt;
        }
        constexpr size_t MAX_FILE_SIZE = 1 * 1024 * 1024; // 1 MB limit
        if (fileSize > MAX_FILE_SIZE)
        {
            vfLogError("Terrain material file '{}' is too large ({} bytes, max {} bytes)",
                       path, fileSize, MAX_FILE_SIZE);
            return std::nullopt;
        }

        std::ifstream file(filePath);
        if (!file.is_open())
        {
            vfLogError("Failed to open terrain material file: {}", path);
            return std::nullopt;
        }

        json j;
        try
        {
            file >> j;
        }
        catch (const json::parse_error& e)
        {
            vfLogError("Terrain material file '{}' contains invalid JSON at byte {}: {}",
                       path, e.byte, e.what());
            return std::nullopt;
        }

        if (!j.is_object())
        {
            vfLogError("Terrain material file '{}' must contain a JSON object at root level", path);
            return std::nullopt;
        }

        TerrainMaterialData material;
        int warningCount = 0;
        constexpr int MAX_WARNINGS = 20;

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
            std::string fileVersion = j.value("version", TERRAIN_MATERIAL_FORMAT_VERSION);
            if (fileVersion != TERRAIN_MATERIAL_FORMAT_VERSION)
            {
                logWarningLimited(std::format(
                    "Terrain material file '{}' has version {} (current is {}). Will migrate on save.",
                    std::string(path), fileVersion, TERRAIN_MATERIAL_FORMAT_VERSION));
            }

            material.uuid = j.value("uuid", std::to_string(uuid::UUID().getValue()));
            material.name = j.value("name", "Unnamed Terrain Material");

            if (material.name.empty())
            {
                material.name = "Unnamed Terrain Material";
                logWarningLimited("Terrain material has empty name, using default");
            }

            int rawLayerCount = j.value("activeLayerCount", 1);
            material.activeLayerCount = static_cast<uint8_t>(
                std::clamp(rawLayerCount, 1, static_cast<int>(MAX_TERRAIN_LAYERS)));

            if (j.contains("layers"))
            {
                if (!j["layers"].is_array())
                {
                    logWarningLimited("'layers' field is not an array, using defaults");
                }
                else
                {
                    const auto& layersJson = j["layers"];
                    size_t count = std::min(layersJson.size(), static_cast<size_t>(MAX_TERRAIN_LAYERS));

                    for (size_t i = 0; i < count; ++i)
                    {
                        const auto& layerJson = layersJson[i];

                        if (!layerJson.is_object())
                        {
                            logWarningLimited(std::format("Layer at index {} is not an object, skipping", i));
                            continue;
                        }

                        try
                        {
                            auto& layer = material.layers[i];
                            layer.albedoTexturePath = layerJson.value("albedoTexturePath", "");
                            layer.normalTexturePath = layerJson.value("normalTexturePath", "");
                            layer.tilingScale = layerJson.value("tilingScale", 1.0f);

                            if (layer.tilingScale <= 0.0f)
                            {
                                logWarningLimited(std::format(
                                    "Layer {} has invalid tilingScale {}, clamping to 0.01",
                                    i, layer.tilingScale));
                                layer.tilingScale = 0.01f;
                            }
                        }
                        catch (const std::exception& e)
                        {
                            logWarningLimited(std::format(
                                "Failed to parse layer at index {}: {}", i, e.what()));
                        }
                    }
                }
            }

            // Parse shader graph
            if (j.contains("graph") && j["graph"].is_object())
            {
                const auto& graphJson = j["graph"];
                std::unordered_map<uint32_t, uint32_t> nodeIdRemap;

                // Parse nodes
                if (graphJson.contains("nodes") && graphJson["nodes"].is_array())
                {
                    std::unordered_set<uint32_t> nodeIds;
                    for (size_t i = 0; i < graphJson["nodes"].size(); ++i)
                    {
                        const auto& nodeJson = graphJson["nodes"][i];
                        if (!nodeJson.is_object()) continue;

                        try
                        {
                            material::ShaderNode node;
                            uint32_t originalId = nodeJson.value("id", 0u);
                            node.id = originalId;

                            if (nodeIds.count(node.id))
                            {
                                node.id = material.graph.nextNodeId++;
                                nodeIdRemap[originalId] = node.id;
                                logWarningLimited(std::format(
                                    "Duplicate node ID {} at index {}, remapped to {}", originalId, i, node.id));
                            }
                            nodeIds.insert(node.id);

                            node.type = material::stringToNodeType(nodeJson.value("type", "ConstantScalar"));
                            node.name = nodeJson.value("name", "");

                            if (nodeJson.contains("position"))
                            {
                                const auto& posJson = nodeJson["position"];
                                if (posJson.is_array() && posJson.size() >= 2 &&
                                    posJson[0].is_number() && posJson[1].is_number())
                                {
                                    node.position.x = posJson[0].get<float>();
                                    node.position.y = posJson[1].get<float>();
                                }
                            }

                            if (nodeJson.contains("properties") && nodeJson["properties"].is_object())
                            {
                                for (auto& [key, val] : nodeJson["properties"].items())
                                {
                                    std::string context = std::format("node {}.{}", node.id, key);
                                    node.properties[key] = material::MaterialAsset::deserializeProperty(val, context);
                                }
                            }

                            material.graph.nodes.push_back(std::move(node));
                            material.graph.nextNodeId = std::max(material.graph.nextNodeId, node.id + 1);
                        }
                        catch (const std::exception& e)
                        {
                            logWarningLimited(std::format("Failed to parse graph node at index {}: {}", i, e.what()));
                        }
                    }
                }

                // Parse links
                if (graphJson.contains("links") && graphJson["links"].is_array())
                {
                    std::unordered_set<uint32_t> validNodeIds;
                    for (const auto& node : material.graph.nodes)
                        validNodeIds.insert(node.id);

                    auto remapNodeId = [&](uint32_t id) -> uint32_t
                    {
                        auto it = nodeIdRemap.find(id);
                        return (it != nodeIdRemap.end()) ? it->second : id;
                    };

                    for (size_t i = 0; i < graphJson["links"].size(); ++i)
                    {
                        const auto& linkJson = graphJson["links"][i];
                        if (!linkJson.is_object()) continue;

                        try
                        {
                            material::NodeLink link;
                            link.id = linkJson.value("id", 0u);
                            link.sourceNodeId = remapNodeId(linkJson.value("sourceNode", 0u));
                            link.targetNodeId = remapNodeId(linkJson.value("targetNode", 0u));
                            link.sourcePin = linkJson.value("sourcePin", "");
                            link.targetPin = linkJson.value("targetPin", "");

                            if (!validNodeIds.count(link.sourceNodeId) || !validNodeIds.count(link.targetNodeId))
                                continue;
                            if (link.sourcePin.empty() || link.targetPin.empty())
                                continue;

                            material.graph.links.push_back(std::move(link));
                            material.graph.nextLinkId = std::max(material.graph.nextLinkId, link.id + 1);
                        }
                        catch (const std::exception& e)
                        {
                            logWarningLimited(std::format("Failed to parse graph link at index {}: {}", i, e.what()));
                        }
                    }
                }
            }

            // Ensure TerrainPBROutput node exists
            if (!material.graph.findTerrainOutputNode())
            {
                material::ShaderNode outputNode;
                outputNode.id = material.graph.nextNodeId++;
                outputNode.type = material::NodeType::TerrainPBROutput;
                outputNode.name = "Terrain PBR Output";
                outputNode.position = glm::vec2(300.0f, 200.0f);
                material.graph.nodes.push_back(std::move(outputNode));
            }

            // Parse cached shader snippet
            material.cachedMaterialSnippet = j.value("cachedMaterialSnippet", "");
            material.needsRecompile = material.cachedMaterialSnippet.empty();

            if (warningCount > 0)
            {
                vfLogWarning("Loaded terrain material '{}' with {} warning(s)", material.name, warningCount);
            }
            return material;
        }
        catch (const json::exception& e)
        {
            vfLogError("Failed to parse terrain material file '{}': {}", path, e.what());
            return std::nullopt;
        }
        catch (const std::exception& e)
        {
            vfLogError("Unexpected error loading terrain material '{}': {}", path, e.what());
            return std::nullopt;
        }
    }

    bool TerrainMaterialAsset::save(std::string_view path, const TerrainMaterialData& material)
    {
        json j;

        j["version"] = TERRAIN_MATERIAL_FORMAT_VERSION;
        j["uuid"] = material.uuid;
        j["name"] = material.name;
        j["activeLayerCount"] = material.activeLayerCount;

        json layersJson = json::array();
        for (int i = 0; i < MAX_TERRAIN_LAYERS; ++i)
        {
            const auto& layer = material.layers[i];
            json layerJson;
            layerJson["albedoTexturePath"] = layer.albedoTexturePath;
            layerJson["normalTexturePath"] = layer.normalTexturePath;
            layerJson["tilingScale"] = layer.tilingScale;
            layersJson.push_back(layerJson);
        }
        j["layers"] = layersJson;

        // Shader graph
        json graphJson;
        json nodesJson = json::array();
        for (const auto& node : material.graph.nodes)
        {
            json nodeJson;
            nodeJson["id"] = node.id;
            nodeJson["type"] = material::nodeTypeToString(node.type);
            nodeJson["name"] = node.name;
            nodeJson["position"] = json::array({node.position.x, node.position.y});

            if (!node.properties.empty())
            {
                json propsJson;
                for (const auto& [key, val] : node.properties)
                {
                    propsJson[key] = material::MaterialAsset::serializeProperty(val);
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

        // Cached shader snippet
        if (!material.cachedMaterialSnippet.empty())
        {
            j["cachedMaterialSnippet"] = material.cachedMaterialSnippet;
        }

        try
        {
            fs::path filePath(path);
            fs::create_directories(filePath.parent_path());

            std::ofstream file(filePath);
            if (!file.is_open())
            {
                vfLogError("Failed to create terrain material file: {}", path);
                return false;
            }

            file << j.dump(4);

            file.flush();
            if (!file.good())
            {
                vfLogError("Failed to flush terrain material file: {}", path);
                return false;
            }

            file.close();
            if (file.fail())
            {
                vfLogError("Failed to close terrain material file: {}", path);
                return false;
            }

            vfLogInfo("Saved terrain material: {} to {}", material.name, path);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save terrain material file {}: {}", path, e.what());
            return false;
        }
    }

    TerrainMaterialData TerrainMaterialAsset::createDefault(const std::string& name)
    {
        TerrainMaterialData material;
        material.uuid = std::to_string(uuid::UUID().getValue());
        material.name = name;
        material.activeLayerCount = 1;
        material.needsRecompile = true;

        // Create default Layer Stack node
        material::ShaderNode layerStackNode;
        layerStackNode.id = material.graph.nextNodeId++;
        layerStackNode.type = material::NodeType::TerrainLayerStack;
        layerStackNode.name = "Layer Stack";
        layerStackNode.position = glm::vec2(0.0f, 200.0f);
        layerStackNode.properties["layerCount"] = material::NodeProperty(1.0f);
        uint32_t layerStackId = layerStackNode.id;
        material.graph.nodes.push_back(std::move(layerStackNode));

        // Create default TerrainPBROutput node
        material::ShaderNode outputNode;
        outputNode.id = material.graph.nextNodeId++;
        outputNode.type = material::NodeType::TerrainPBROutput;
        outputNode.name = "Terrain PBR Output";
        outputNode.position = glm::vec2(400.0f, 200.0f);
        uint32_t outputId = outputNode.id;
        material.graph.nodes.push_back(std::move(outputNode));

        // Links will be created after pins are initialized in the editor
        // (pins don't exist yet at this point - they get created by ShaderNodeFactory::initializeNode)

        return material;
    }
}
