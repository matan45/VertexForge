#include "MaterialGraphHelper.hpp"

namespace material
{
    std::optional<NodeProperty> MaterialGraphHelper::getConnectedValue(
        const ShaderGraph& graph,
        uint32_t targetNodeId,
        const std::string& targetPinName)
    {
        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == targetNodeId && link.targetPin == targetPinName)
            {
                const auto* sourceNode = graph.findNode(link.sourceNodeId);
                if (!sourceNode) continue;

                switch (sourceNode->type)
                {
                case NodeType::ConstantScalar:
                case NodeType::ConstantVec2:
                case NodeType::ConstantVec3:
                case NodeType::ConstantColor:
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

    std::string MaterialGraphHelper::getConnectedTexturePath(
        const ShaderGraph& graph,
        uint32_t targetNodeId,
        const std::string& targetPinName)
    {
        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == targetNodeId && link.targetPin == targetPinName)
            {
                const auto* sourceNode = graph.findNode(link.sourceNodeId);
                if (!sourceNode) continue;

                if (sourceNode->type == NodeType::TextureSample)
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

    std::string MaterialGraphHelper::getOrmTexturePath(
        const ShaderGraph& graph,
        uint32_t targetNodeId)
    {
        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == targetNodeId)
            {
                const auto* sourceNode = graph.findNode(link.sourceNodeId);
                if (!sourceNode) continue;

                if (sourceNode->type == NodeType::OrmSample)
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

    ExtractedParentPBR MaterialGraphHelper::extractPBRFromGraph(const MaterialData& matData)
    {
        ExtractedParentPBR pbr;

        const auto* outputNode = matData.graph.findOutputNode();
        if (!outputNode)
        {
            return pbr;
        }

        // Extract albedo
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

        // Extract metallic
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Metallic"))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.metallic = std::get<float>(*val);
            }
        }

        // Extract roughness
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Roughness"))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.roughness = std::get<float>(*val);
            }
        }

        // Extract AO
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "AO"))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.ao = std::get<float>(*val);
            }
        }

        // Extract emission
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

        // Extract IBL values
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

        // Fallback to material parameters for albedo
        if (pbr.albedo == glm::vec4(1.0f))
        {
            auto findParam = [&matData](const std::string& name) -> const MaterialParameter*
            {
                auto it = matData.parameters.find(name);
                return (it != matData.parameters.end()) ? &it->second : nullptr;
            };

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

        // Extract texture paths
        pbr.albedoTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Albedo");
        pbr.normalTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Normal");
        pbr.metallicTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Metallic");
        pbr.roughnessTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Roughness");
        pbr.aoTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "AO");
        pbr.emissionTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Emission");
        pbr.heightTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Height");
        pbr.ormTexturePath = getOrmTexturePath(matData.graph, outputNode->id);

        return pbr;
    }
}
