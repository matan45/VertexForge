#include "MaterialGraphEvaluator.hpp"
#include <algorithm>

namespace editor::materialeditor
{
    std::string MaterialGraphEvaluator::cleanPath(const std::string& path)
    {
        std::string clean = path;
        clean.erase(std::remove(clean.begin(), clean.end(), '\0'), clean.end());
        return clean;
    }

    std::optional<float> MaterialGraphEvaluator::getConnectedScalar(
        const ::material::ShaderGraph& graph,
        const ::material::ShaderNode* pbrOutput,
        const std::string& pinName)
    {
        for (const auto& pin : pbrOutput->inputs) {
            if (pin.name == pinName) {
                for (const auto& link : graph.links) {
                    if (link.targetNodeId == pbrOutput->id && link.targetPin == pinName) {
                        for (const auto& node : graph.nodes) {
                            if (node.id == link.sourceNodeId) {
                                if (node.type == ::material::NodeType::ConstantScalar) {
                                    auto it = node.properties.find("value");
                                    if (it != node.properties.end() && std::holds_alternative<float>(it->second)) {
                                        return std::get<float>(it->second);
                                    }
                                }
                                break;
                            }
                        }
                        break;
                    }
                }
                break;
            }
        }
        return std::nullopt;
    }

    std::optional<glm::vec4> MaterialGraphEvaluator::getConnectedColor(
        const ::material::ShaderGraph& graph,
        const ::material::ShaderNode* pbrOutput,
        const std::string& pinName)
    {
        for (const auto& pin : pbrOutput->inputs) {
            if (pin.name == pinName) {
                for (const auto& link : graph.links) {
                    if (link.targetNodeId == pbrOutput->id && link.targetPin == pinName) {
                        for (const auto& node : graph.nodes) {
                            if (node.id == link.sourceNodeId) {
                                if (node.type == ::material::NodeType::ConstantColor) {
                                    auto it = node.properties.find("value");
                                    if (it != node.properties.end() && std::holds_alternative<glm::vec4>(it->second)) {
                                        return std::get<glm::vec4>(it->second);
                                    }
                                }
                                else if (node.type == ::material::NodeType::ConstantVec3) {
                                    auto it = node.properties.find("value");
                                    if (it != node.properties.end() && std::holds_alternative<glm::vec3>(it->second)) {
                                        glm::vec3 vec = std::get<glm::vec3>(it->second);
                                        return glm::vec4(vec, 1.0f);
                                    }
                                }
                                break;
                            }
                        }
                        break;
                    }
                }
                break;
            }
        }
        return std::nullopt;
    }

    std::string MaterialGraphEvaluator::getConnectedTexturePath(
        const ::material::ShaderGraph& graph,
        const ::material::ShaderNode* pbrOutput,
        const std::string& pinName)
    {
        for (const auto& link : graph.links) {
            if (link.targetNodeId == pbrOutput->id && link.targetPin == pinName) {
                for (const auto& node : graph.nodes) {
                    if (node.id == link.sourceNodeId &&
                        (node.type == ::material::NodeType::TextureSample ||
                         node.type == ::material::NodeType::OrmSample)) {
                        auto it = node.properties.find("texturePath");
                        if (it != node.properties.end() && std::holds_alternative<std::string>(it->second)) {
                            return cleanPath(std::get<std::string>(it->second));
                        }
                    }
                }
            }
        }
        return "";
    }

    std::string MaterialGraphEvaluator::getOrmTexturePath(
        const ::material::ShaderGraph& graph,
        const ::material::ShaderNode* pbrOutput)
    {
        for (const auto& link : graph.links) {
            if (link.targetNodeId == pbrOutput->id) {
                for (const auto& node : graph.nodes) {
                    if (node.id == link.sourceNodeId && node.type == ::material::NodeType::OrmSample) {
                        auto it = node.properties.find("texturePath");
                        if (it != node.properties.end() && std::holds_alternative<std::string>(it->second)) {
                            return cleanPath(std::get<std::string>(it->second));
                        }
                    }
                }
            }
        }
        return "";
    }

    GraphEvaluationResult MaterialGraphEvaluator::evaluate(const ::material::ShaderGraph& graph)
    {
        GraphEvaluationResult result;

        const ::material::ShaderNode* pbrOutput = nullptr;
        for (const auto& node : graph.nodes) {
            if (node.type == ::material::NodeType::PBROutput) {
                pbrOutput = &node;
                break;
            }
        }

        if (!pbrOutput) {
            return result;
        }

        result.albedo = getConnectedColor(graph, pbrOutput, "Albedo");
        result.metallic = getConnectedScalar(graph, pbrOutput, "Metallic");
        result.roughness = getConnectedScalar(graph, pbrOutput, "Roughness");
        result.ao = getConnectedScalar(graph, pbrOutput, "AO");
        result.emission = getConnectedScalar(graph, pbrOutput, "Emission");

        result.albedoTexturePath = getConnectedTexturePath(graph, pbrOutput, "Albedo");
        result.normalTexturePath = getConnectedTexturePath(graph, pbrOutput, "Normal");
        result.emissionTexturePath = getConnectedTexturePath(graph, pbrOutput, "Emission");

        result.heightTexturePath = getConnectedTexturePath(graph, pbrOutput, "Displacement");
        if (result.heightTexturePath.empty()) {
            result.heightTexturePath = getConnectedTexturePath(graph, pbrOutput, "Height");
        }

        std::string ormPath = getOrmTexturePath(graph, pbrOutput);
        if (!ormPath.empty()) {
            result.ormTexturePath = ormPath;
        } else {
            result.metallicTexturePath = getConnectedTexturePath(graph, pbrOutput, "Metallic");
            result.roughnessTexturePath = getConnectedTexturePath(graph, pbrOutput, "Roughness");
            result.aoTexturePath = getConnectedTexturePath(graph, pbrOutput, "AO");
        }

        return result;
    }

    services::MaterialPreviewParams MaterialGraphEvaluator::toPreviewParams(
        const GraphEvaluationResult& result,
        const std::string& materialPath,
        std::shared_ptr<::material::MaterialData> materialData,
        bool useCustomShader)
    {
        services::MaterialPreviewParams params;
        params.useCustomShader = useCustomShader;

        if (result.albedo) {
            params.albedo = *result.albedo;
        }
        if (result.metallic) {
            params.metallic = *result.metallic;
        }
        if (result.roughness) {
            params.roughness = *result.roughness;
        }
        if (result.ao) {
            params.ao = *result.ao;
        }
        if (result.emission) {
            params.emission = *result.emission;
        }

        params.albedoTexturePath = result.albedoTexturePath;
        params.normalTexturePath = result.normalTexturePath;
        params.emissionTexturePath = result.emissionTexturePath;
        params.heightTexturePath = result.heightTexturePath;

        if (!result.ormTexturePath.empty()) {
            params.ormTexturePath = result.ormTexturePath;
        } else {
            params.metallicTexturePath = result.metallicTexturePath;
            params.roughnessTexturePath = result.roughnessTexturePath;
            params.aoTexturePath = result.aoTexturePath;
        }

        params.materialPath = materialPath;
        params.materialDataHandle = materialData;

        return params;
    }
}
