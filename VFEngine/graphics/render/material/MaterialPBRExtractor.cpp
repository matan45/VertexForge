#include "MaterialPBRExtractor.hpp"
#include "material/MaterialManager.hpp"
#include "resource/ResourceManager.hpp"
#include <cmath>
#include <vector>

namespace render::mesh
{
    std::optional<float> MaterialPBRExtractor::getInputFloat(const material::ShaderGraph& graph,
                                                             uint32_t nodeId, const std::string& pinName,
                                                             float time)
    {
        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == nodeId && link.targetPin == pinName)
            {
                return evaluateFloatValue(graph, link.sourceNodeId, time);
            }
        }
        return std::nullopt;
    }

    std::optional<float> MaterialPBRExtractor::evaluateFloatValue(const material::ShaderGraph& graph,
                                                                  uint32_t nodeId, float time)
    {
        const auto* node = graph.findNode(nodeId);
        if (!node) return std::nullopt;

        switch (node->type)
        {
        case material::NodeType::ConstantScalar:
            {
                auto it = node->properties.find("value");
                if (it != node->properties.end() && std::holds_alternative<float>(it->second))
                {
                    return std::get<float>(it->second);
                }
                break;
            }
        case material::NodeType::Time:
            {
                return time;
            }
        case material::NodeType::Sin:
            {
                auto inputVal = getInputFloat(graph, nodeId, "Value", time);
                if (inputVal)
                {
                    return std::sin(*inputVal);
                }
                return 0.0f;
            }
        case material::NodeType::Cos:
            {
                auto inputVal = getInputFloat(graph, nodeId, "Value", time);
                if (inputVal)
                {
                    return std::cos(*inputVal);
                }
                return 0.0f;
            }
        case material::NodeType::Multiply:
            {
                auto a = getInputFloat(graph, nodeId, "A", time);
                auto b = getInputFloat(graph, nodeId, "B", time);
                float aVal = a.value_or(1.0f);
                float bVal = b.value_or(1.0f);
                return aVal * bVal;
            }
        case material::NodeType::Add:
            {
                auto a = getInputFloat(graph, nodeId, "A", time);
                auto b = getInputFloat(graph, nodeId, "B", time);
                float aVal = a.value_or(0.0f);
                float bVal = b.value_or(0.0f);
                return aVal + bVal;
            }
        default:
            break;
        }
        return std::nullopt;
    }

    std::optional<material::NodeProperty> MaterialPBRExtractor::getConnectedValue(const material::ShaderGraph& graph,
        uint32_t targetNodeId, const std::string& targetPinName)
    {
        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == targetNodeId && link.targetPin == targetPinName)
            {
                const auto* sourceNode = graph.findNode(link.sourceNodeId);
                if (!sourceNode) continue;

                switch (sourceNode->type)
                {
                case material::NodeType::ConstantScalar:
                    {
                        auto it = sourceNode->properties.find("value");
                        if (it != sourceNode->properties.end())
                        {
                            return it->second;
                        }
                        break;
                    }
                case material::NodeType::ConstantVec2:
                case material::NodeType::ConstantVec3:
                case material::NodeType::ConstantColor:
                    {
                        auto it = sourceNode->properties.find("value");
                        if (it != sourceNode->properties.end())
                        {
                            return it->second;
                        }
                        break;
                    }
                default:
                    break;
                }
            }
        }
        return std::nullopt;
    }

    float MaterialPBRExtractor::evaluateEmissionStrength(const material::ShaderGraph& graph,
                                                         uint32_t outputNodeId, float time)
    {
        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == outputNodeId && link.targetPin == "EmissionStrength")
            {
                auto val = evaluateFloatValue(graph, link.sourceNodeId, time);
                if (val)
                {
                    return *val;
                }
            }
        }
        return 0.0f;
    }

    std::string MaterialPBRExtractor::getConnectedTexturePath(const material::ShaderGraph& graph,
                                                              uint32_t targetNodeId, const std::string& targetPinName)
    {
        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == targetNodeId && link.targetPin == targetPinName)
            {
                const auto* sourceNode = graph.findNode(link.sourceNodeId);
                if (!sourceNode) continue;

                if (sourceNode->type == material::NodeType::TextureSample)
                {
                    auto it = sourceNode->properties.find("texturePath");
                    if (it != sourceNode->properties.end() &&
                        std::holds_alternative<std::string>(it->second))
                    {
                        return std::get<std::string>(it->second);
                    }
                }
            }
        }
        return "";
    }


    std::string getOrmTexturePath(const material::ShaderGraph& graph, uint32_t targetNodeId)
    {
        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == targetNodeId)
            {
                const auto* sourceNode = graph.findNode(link.sourceNodeId);
                if (!sourceNode) continue;

                if (sourceNode->type == material::NodeType::OrmSample)
                {
                    auto it = sourceNode->properties.find("texturePath");
                    if (it != sourceNode->properties.end() &&
                        std::holds_alternative<std::string>(it->second))
                    {
                        return std::get<std::string>(it->second);
                    }
                }
            }
        }
        return "";
    }

    ExtractedPBRValues MaterialPBRExtractor::extractPBRFromMaterial(const material::MaterialData& matData)
    {
        ExtractedPBRValues pbr;

        const auto* outputNode = matData.graph.findOutputNode();
        if (!outputNode)
        {
            return pbr;
        }

        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Albedo"))
        {
            if (std::holds_alternative<glm::vec4>(*val))
            {
                pbr.albedo = std::get<glm::vec4>(*val);
            }
            else if (std::holds_alternative<glm::vec3>(*val))
            {
                glm::vec3 rgb = std::get<glm::vec3>(*val);
                pbr.albedo = glm::vec4(rgb, 1.0f);
            }
        }

        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Metallic"))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.metallic = std::get<float>(*val);
            }
        }

        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Roughness"))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.roughness = std::get<float>(*val);
            }
        }

        if (auto val = getConnectedValue(matData.graph, outputNode->id, "AO"))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.ao = std::get<float>(*val);
            }
        }

        float emissionStrength = 0.0f;
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "EmissionStrength"))
        {
            if (std::holds_alternative<float>(*val))
            {
                emissionStrength = std::get<float>(*val);
            }
        }
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Emission"))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.emission = std::get<float>(*val) * emissionStrength;
            }
            else if (std::holds_alternative<glm::vec3>(*val))
            {
                glm::vec3 emissionColor = std::get<glm::vec3>(*val);
                pbr.emission = (emissionColor.r * 0.299f + emissionColor.g * 0.587f + emissionColor.b * 0.114f) *
                    emissionStrength;
            }
            else if (std::holds_alternative<glm::vec4>(*val))
            {
                glm::vec4 emissionColor = std::get<glm::vec4>(*val);
                pbr.emission = (emissionColor.r * 0.299f + emissionColor.g * 0.587f + emissionColor.b * 0.114f) *
                    emissionStrength;
            }
        }
        else if (emissionStrength > 0.0f)
        {
            pbr.emission = emissionStrength;
        }

        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Opacity"))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.albedo.a = std::get<float>(*val);
            }
        }

        if (auto val = getConnectedValue(matData.graph, outputNode->id, "IBLDiffuse"))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.iblDiffuse = std::get<float>(*val);
            }
        }


        if (auto val = getConnectedValue(matData.graph, outputNode->id, "IBLSpecular"))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.iblSpecular = std::get<float>(*val);
            }
        }

        auto findParam = [&matData](const std::string& name) -> const material::MaterialParameter*
        {
            auto it = matData.parameters.find(name);
            return (it != matData.parameters.end()) ? &it->second : nullptr;
        };

        if (pbr.albedo == glm::vec4(1.0f))
        {
            if (const auto* param = findParam("Albedo"))
            {
                if (std::holds_alternative<glm::vec4>(param->value))
                {
                    pbr.albedo = std::get<glm::vec4>(param->value);
                }
            }
            else if (const auto* param = findParam("BaseColor"))
            {
                if (std::holds_alternative<glm::vec4>(param->value))
                {
                    pbr.albedo = std::get<glm::vec4>(param->value);
                }
            }
        }

        pbr.albedoTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Albedo");
        pbr.normalTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Normal");
        pbr.metallicTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Metallic");
        pbr.roughnessTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Roughness");
        pbr.aoTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "AO");
        pbr.emissionTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Emission");
        pbr.heightTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Height");

        pbr.ormTexturePath = getOrmTexturePath(matData.graph, outputNode->id);

        pbr.blendMode = matData.blendMode;

        return pbr;
    }

    ExtractedPBRValues MaterialPBRExtractor::getPBRForSubmesh(
        const MeshRenderData& meshData,
        const std::string& submeshName,
        const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& matCache,
        float time)
    {
        ExtractedPBRValues pbr;
        pbr.albedo = meshData.albedo;
        pbr.metallic = meshData.metallic;
        pbr.roughness = meshData.roughness;
        pbr.ao = meshData.ao;
        pbr.emission = meshData.emission;

        std::string materialPath;


        const auto* submeshMat = meshData.getMaterialForSubmesh(submeshName);
        if (submeshMat && !submeshMat->materialPath.empty())
        {
            materialPath = submeshMat->materialPath;
        }

        else if (!meshData.defaultMaterialPath.empty())
        {
            materialPath = meshData.defaultMaterialPath;
        }


        if (!materialPath.empty())
        {
            std::shared_ptr<material::MaterialData> matData;

            // Look up material in cache (read-only)
            auto cacheIt = matCache.find(materialPath);
            if (cacheIt != matCache.end() && cacheIt->second)
            {
                matData = cacheIt->second;
                pbr = extractPBRFromMaterial(*matData);
            }

            // Evaluate dynamic emission strength (Time, Sin, Cos nodes)
            if (matData)
            {
                const auto* outputNode = matData->graph.findOutputNode();
                if (outputNode)
                {
                    float dynamicEmission = evaluateEmissionStrength(matData->graph, outputNode->id, time);
                    if (dynamicEmission != 0.0f)
                    {
                        pbr.emission = dynamicEmission;
                    }
                }
            }

            pbr.materialPath = materialPath;
        }

        return pbr;
    }
}
