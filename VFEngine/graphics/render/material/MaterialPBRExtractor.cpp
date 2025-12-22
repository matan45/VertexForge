#include "MaterialPBRExtractor.hpp"
#include "material/MaterialManager.hpp"
#include "resource/ResourceManager.hpp"
#include <cmath>

namespace render::mesh
{
    std::optional<float> MaterialPBRExtractor::getInputFloat(
        const material::ShaderGraph& graph,
        uint32_t nodeId,
        const std::string& pinName,
        float time)
    {
        for (const auto& link : graph.links) {
            if (link.targetNodeId == nodeId && link.targetPin == pinName) {
                return evaluateFloatValue(graph, link.sourceNodeId, link.sourcePin, time);
            }
        }
        return std::nullopt;
    }

    std::optional<float> MaterialPBRExtractor::evaluateFloatValue(
        const material::ShaderGraph& graph,
        uint32_t nodeId,
        const std::string& pinName,
        float time)
    {
        const auto* node = graph.findNode(nodeId);
        if (!node) return std::nullopt;

        switch (node->type) {
            case material::NodeType::ConstantScalar: {
                auto it = node->properties.find("value");
                if (it != node->properties.end() && std::holds_alternative<float>(it->second)) {
                    return std::get<float>(it->second);
                }
                break;
            }
            case material::NodeType::Time: {
                return time;
            }
            case material::NodeType::Sin: {
                auto inputVal = getInputFloat(graph, nodeId, "Value", time);
                if (inputVal) {
                    return std::sin(*inputVal);
                }
                return 0.0f;
            }
            case material::NodeType::Cos: {
                auto inputVal = getInputFloat(graph, nodeId, "Value", time);
                if (inputVal) {
                    return std::cos(*inputVal);
                }
                return 0.0f;
            }
            case material::NodeType::Multiply: {
                auto a = getInputFloat(graph, nodeId, "A", time);
                auto b = getInputFloat(graph, nodeId, "B", time);
                float aVal = a.value_or(1.0f);
                float bVal = b.value_or(1.0f);
                return aVal * bVal;
            }
            case material::NodeType::Add: {
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

    std::optional<material::NodeProperty> MaterialPBRExtractor::getConnectedValue(
        const material::ShaderGraph& graph,
        uint32_t targetNodeId,
        const std::string& targetPinName)
    {
        // Find link to this pin
        for (const auto& link : graph.links) {
            if (link.targetNodeId == targetNodeId && link.targetPin == targetPinName) {
                // Found a connection - get the source node
                const auto* sourceNode = graph.findNode(link.sourceNodeId);
                if (!sourceNode) continue;

                // Check if it's a constant node and get its value
                switch (sourceNode->type) {
                    case material::NodeType::ConstantScalar: {
                        auto it = sourceNode->properties.find("value");
                        if (it != sourceNode->properties.end()) {
                            return it->second;
                        }
                        break;
                    }
                    case material::NodeType::ConstantVec2:
                    case material::NodeType::ConstantVec3:
                    case material::NodeType::ConstantColor: {
                        auto it = sourceNode->properties.find("value");
                        if (it != sourceNode->properties.end()) {
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

    float MaterialPBRExtractor::evaluateEmissionStrength(
        const material::ShaderGraph& graph,
        uint32_t outputNodeId,
        float time)
    {
        for (const auto& link : graph.links) {
            if (link.targetNodeId == outputNodeId && link.targetPin == "EmissionStrength") {
                auto val = evaluateFloatValue(graph, link.sourceNodeId, link.sourcePin, time);
                if (val) {
                    return *val;
                }
            }
        }
        return 0.0f;
    }

    std::string MaterialPBRExtractor::getConnectedTexturePath(
        const material::ShaderGraph& graph,
        uint32_t targetNodeId,
        const std::string& targetPinName)
    {
        for (const auto& link : graph.links) {
            if (link.targetNodeId == targetNodeId && link.targetPin == targetPinName) {
                const auto* sourceNode = graph.findNode(link.sourceNodeId);
                if (!sourceNode) continue;

                if (sourceNode->type == material::NodeType::TextureSample) {
                    auto it = sourceNode->properties.find("texturePath");
                    if (it != sourceNode->properties.end() &&
                        std::holds_alternative<std::string>(it->second)) {
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

        // Find PBR Output node
        const auto* outputNode = matData.graph.findOutputNode();
        if (!outputNode) {
            return pbr;  // Return defaults if no output node
        }

        // Try to get Albedo from connected node
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Albedo")) {
            if (std::holds_alternative<glm::vec4>(*val)) {
                pbr.albedo = std::get<glm::vec4>(*val);
            } else if (std::holds_alternative<glm::vec3>(*val)) {
                glm::vec3 rgb = std::get<glm::vec3>(*val);
                pbr.albedo = glm::vec4(rgb, 1.0f);
            }
        }

        // Try to get Metallic
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Metallic")) {
            if (std::holds_alternative<float>(*val)) {
                pbr.metallic = std::get<float>(*val);
            }
        }

        // Try to get Roughness
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Roughness")) {
            if (std::holds_alternative<float>(*val)) {
                pbr.roughness = std::get<float>(*val);
            }
        }

        // Try to get AO
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "AO")) {
            if (std::holds_alternative<float>(*val)) {
                pbr.ao = std::get<float>(*val);
            }
        }

        // Try to get Emission (Vec3) and EmissionStrength (Float)
        float emissionStrength = 0.0f;
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "EmissionStrength")) {
            if (std::holds_alternative<float>(*val)) {
                emissionStrength = std::get<float>(*val);
            }
        }
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Emission")) {
            if (std::holds_alternative<float>(*val)) {
                // Single float emission value
                pbr.emission = std::get<float>(*val) * emissionStrength;
            } else if (std::holds_alternative<glm::vec3>(*val)) {
                // Vec3 emission - use luminance approximation
                glm::vec3 emissionColor = std::get<glm::vec3>(*val);
                pbr.emission = (emissionColor.r * 0.299f + emissionColor.g * 0.587f + emissionColor.b * 0.114f) * emissionStrength;
            } else if (std::holds_alternative<glm::vec4>(*val)) {
                // Vec4 emission - use luminance approximation
                glm::vec4 emissionColor = std::get<glm::vec4>(*val);
                pbr.emission = (emissionColor.r * 0.299f + emissionColor.g * 0.587f + emissionColor.b * 0.114f) * emissionStrength;
            }
        } else if (emissionStrength > 0.0f) {
            // No emission color connected but strength is set - use white emission
            pbr.emission = emissionStrength;
        }

        // Try to get Opacity
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Opacity")) {
            if (std::holds_alternative<float>(*val)) {
                pbr.albedo.a = std::get<float>(*val);
            }
        }

        // Try to get IBL Diffuse intensity
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "IBLDiffuse")) {
            if (std::holds_alternative<float>(*val)) {
                pbr.iblDiffuse = std::get<float>(*val);
            }
        }

        // Try to get IBL Specular intensity
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "IBLSpecular")) {
            if (std::holds_alternative<float>(*val)) {
                pbr.iblSpecular = std::get<float>(*val);
            }
        }

        // Also check exposed parameters as fallback
        auto findParam = [&matData](const std::string& name) -> const material::MaterialParameter* {
            auto it = matData.parameters.find(name);
            return (it != matData.parameters.end()) ? &it->second : nullptr;
        };

        // Fallback to parameters if graph values not found
        if (pbr.albedo == glm::vec4(1.0f)) {
            if (const auto* param = findParam("Albedo")) {
                if (std::holds_alternative<glm::vec4>(param->value)) {
                    pbr.albedo = std::get<glm::vec4>(param->value);
                }
            } else if (const auto* param = findParam("BaseColor")) {
                if (std::holds_alternative<glm::vec4>(param->value)) {
                    pbr.albedo = std::get<glm::vec4>(param->value);
                }
            }
        }

        // Extract texture paths from connected TextureSample nodes
        pbr.albedoTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Albedo");
        pbr.normalTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Normal");
        pbr.ormTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "ORM");
        pbr.metallicTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Metallic");
        pbr.roughnessTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Roughness");
        pbr.aoTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "AO");
        pbr.emissionTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Emission");
        pbr.heightTexturePath = getConnectedTexturePath(matData.graph, outputNode->id, "Height");

        // Get blend mode from material
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

        // Check for per-submesh material override
        const auto* submeshMat = meshData.getMaterialForSubmesh(submeshName);
        if (submeshMat && !submeshMat->materialPath.empty()) {
            materialPath = submeshMat->materialPath;
        }
        // Fall back to default material
        else if (!meshData.defaultMaterialPath.empty()) {
            materialPath = meshData.defaultMaterialPath;
        }

        // Extract PBR values from the material (using pre-loaded cache)
        // Note: Materials should be loaded beforehand via MaterialCacheManager
        if (!materialPath.empty()) {
            std::shared_ptr<material::MaterialData> matData;

            // Look up material in cache (read-only)
            auto cacheIt = matCache.find(materialPath);
            if (cacheIt != matCache.end() && cacheIt->second) {
                matData = cacheIt->second;
                pbr = extractPBRFromMaterial(*matData);
            }
            // If not in cache, use defaults (material wasn't pre-loaded)

            // Evaluate dynamic emission strength (Time, Sin, Cos nodes)
            if (matData) {
                const auto* outputNode = matData->graph.findOutputNode();
                if (outputNode) {
                    float dynamicEmission = evaluateEmissionStrength(matData->graph, outputNode->id, time);
                    if (dynamicEmission != 0.0f) {
                        pbr.emission = dynamicEmission;
                    }
                }
            }

            // Store material path for shader cache lookup
            pbr.materialPath = materialPath;
        }

        return pbr;
    }
}
