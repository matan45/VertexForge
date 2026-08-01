#include "MaterialPBRExtractor.hpp"
#include "material/MaterialManager.hpp"
#include "material/MaterialParameterSet.hpp"
#include "resource/ResourceManager.hpp"
#include "asset/AssetRef.hpp"
#include "../gpudriven/scene/ToonProfileGpuTable.hpp"
#include <cmath>
#include <vector>

namespace render::mesh
{
    std::optional<float> MaterialPBRExtractor::getInputFloat(const material::ShaderGraph& graph,
                                                             uint32_t nodeId, const std::string& pinName,
                                                             float time,
                                                             const ParameterOverrides* paramOverrides)
    {
        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == nodeId && link.targetPin == pinName)
            {
                return evaluateFloatValue(graph, link.sourceNodeId, time, paramOverrides);
            }
        }
        return std::nullopt;
    }

    std::optional<float> MaterialPBRExtractor::evaluateFloatValue(const material::ShaderGraph& graph,
                                                                  uint32_t nodeId, float time,
                                                                  const ParameterOverrides* paramOverrides)
    {
        const auto* node = graph.findNode(nodeId);
        if (!node) return std::nullopt;

        switch (node->type)
        {
        case material::NodeType::ConstantScalar:
            {
                if (paramOverrides)
                {
                    if (auto overridden = material::overrideValueForNode(*node, *paramOverrides))
                    {
                        if (const float* v = std::get_if<float>(&*overridden)) return *v;
                    }
                }
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
                auto inputVal = getInputFloat(graph, nodeId, "Value", time, paramOverrides);
                if (inputVal)
                {
                    return std::sin(*inputVal);
                }
                return 0.0f;
            }
        case material::NodeType::Cos:
            {
                auto inputVal = getInputFloat(graph, nodeId, "Value", time, paramOverrides);
                if (inputVal)
                {
                    return std::cos(*inputVal);
                }
                return 0.0f;
            }
        case material::NodeType::Multiply:
            {
                auto a = getInputFloat(graph, nodeId, "A", time, paramOverrides);
                auto b = getInputFloat(graph, nodeId, "B", time, paramOverrides);
                float aVal = a.value_or(1.0f);
                float bVal = b.value_or(1.0f);
                return aVal * bVal;
            }
        case material::NodeType::Add:
            {
                auto a = getInputFloat(graph, nodeId, "A", time, paramOverrides);
                auto b = getInputFloat(graph, nodeId, "B", time, paramOverrides);
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
        uint32_t targetNodeId, const std::string& targetPinName,
        const ParameterOverrides* paramOverrides)
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
                case material::NodeType::ConstantVec2:
                case material::NodeType::ConstantVec3:
                case material::NodeType::ConstantColor:
                    {
                        if (paramOverrides)
                        {
                            if (auto overridden = material::overrideValueForNode(*sourceNode, *paramOverrides))
                            {
                                return std::visit([](const auto& v) -> material::NodeProperty { return v; },
                                                  *overridden);
                            }
                        }
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
                                                         uint32_t outputNodeId, float time,
                                                         const ParameterOverrides* paramOverrides)
    {
        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == outputNodeId && link.targetPin == "EmissionStrength")
            {
                auto val = evaluateFloatValue(graph, link.sourceNodeId, time, paramOverrides);
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

    ExtractedPBRValues MaterialPBRExtractor::extractPBRFromMaterial(
        const material::MaterialData& matData,
        const ParameterOverrides* paramOverrides)
    {
        ExtractedPBRValues pbr;

        const auto* outputNode = matData.graph.findOutputNode();
        if (!outputNode)
        {
            return pbr;
        }

        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Albedo", paramOverrides))
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

        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Metallic", paramOverrides))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.metallic = std::get<float>(*val);
            }
        }

        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Roughness", paramOverrides))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.roughness = std::get<float>(*val);
            }
        }

        if (auto val = getConnectedValue(matData.graph, outputNode->id, "AO", paramOverrides))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.ao = std::get<float>(*val);
            }
        }

        float emissionStrength = 0.0f;
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "EmissionStrength", paramOverrides))
        {
            if (std::holds_alternative<float>(*val))
            {
                emissionStrength = std::get<float>(*val);
            }
        }
        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Emission", paramOverrides))
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

        if (auto val = getConnectedValue(matData.graph, outputNode->id, "Opacity", paramOverrides))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.albedo.a = std::get<float>(*val);
            }
        }

        if (auto val = getConnectedValue(matData.graph, outputNode->id, "IBLDiffuse", paramOverrides))
        {
            if (std::holds_alternative<float>(*val))
            {
                pbr.iblDiffuse = std::get<float>(*val);
            }
        }


        if (auto val = getConnectedValue(matData.graph, outputNode->id, "IBLSpecular", paramOverrides))
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
        pbr.opacity = matData.opacity;
        pbr.alphaCutoff = matData.alphaCutoff;

        // VK-1493: shading model + resolved toon profile slot. resolveIndex assigns a
        // stable 0-127 GPU slot (loading the profile on first touch). This runs on the
        // main thread during the sequential pbr-cache pre-warm, so the later parallel
        // flag-packing only reads the already-resolved index. The table is null in
        // CPU-only contexts (e.g. Tests) — index then stays 0 (default), which is safe.
        pbr.shadingModel = static_cast<uint8_t>(matData.shadingModel);
        if (matData.shadingModel == material::ShadingModel::Toon)
        {
            if (auto* toonTable = render::gpudriven::ToonProfileGpuTable::active())
            {
                pbr.toonProfileIndex = toonTable->resolveIndex(matData.toonProfile);
            }
        }

        // VK-1580: foliage-wind gate. Global wind params, so this is a pure copy — no GPU
        // table to resolve. The merged-scene packer turns it into the FoliageWind flag bit.
        pbr.receiveWind = matData.receiveWind;

        // VK-1620: mesh-into-terrain blending. Also a pure copy — the terrain side of the blend is
        // resolved entirely on the GPU from the RVT, so nothing here needs a table lookup. Clamped
        // because this is the last point before the values are packed into halfs and the shader
        // trusts them without re-clamping.
        pbr.blendToTerrain = matData.blendToTerrain;
        pbr.terrainBlendBand = material::clampTerrainBlendBand(matData.terrainBlendBand);
        pbr.terrainBlendContrast = material::clampTerrainBlendContrast(matData.terrainBlendContrast);

        return pbr;
    }

    ExtractedPBRValues MaterialPBRExtractor::extractPBRFromInstance(
        const material::MaterialInstanceData& instance,
        const material::MaterialData& parentMaterial,
        const ParameterOverrides* runtimeOverrides)
    {
        // Named parameter overrides apply during the parent graph walk; the fixed PBR
        // scalar overrides below still win on top (they target PBROutput inputs directly).
        material::MaterialParameterSet parentSet = material::collectParameters(parentMaterial.graph);
        ParameterOverrides resolved = material::resolveOverrides(parentSet, &instance, runtimeOverrides);

        ExtractedPBRValues pbr = extractPBRFromMaterial(parentMaterial,
                                                        resolved.empty() ? nullptr : &resolved);

        // Apply scalar overrides
        if (instance.albedoOverride.has_value())
        {
            pbr.albedo = *instance.albedoOverride;
        }
        if (instance.metallicOverride.has_value())
        {
            pbr.metallic = *instance.metallicOverride;
        }
        if (instance.roughnessOverride.has_value())
        {
            pbr.roughness = *instance.roughnessOverride;
        }
        if (instance.aoOverride.has_value())
        {
            pbr.ao = *instance.aoOverride;
        }
        if (instance.emissionOverride.has_value())
        {
            pbr.emission = *instance.emissionOverride;
        }
        if (instance.iblDiffuseOverride.has_value())
        {
            pbr.iblDiffuse = *instance.iblDiffuseOverride;
        }
        if (instance.iblSpecularOverride.has_value())
        {
            pbr.iblSpecular = *instance.iblSpecularOverride;
        }

        // Apply texture overrides (named texture parameters resolved onto slots,
        // legacy slot-addressed overrides winning ties)
        std::map<material::TextureSlot, asset::AssetRef> effectiveTextures =
            material::resolveTextureOverrides(parentSet, instance);
        for (const auto& [slot, texPath] : effectiveTextures)
        {
            if (!texPath.isValid()) continue;

            switch (slot)
            {
            case material::TextureSlot::Albedo:
                pbr.albedoTexturePath = texPath.resolve();
                break;
            case material::TextureSlot::Normal:
                pbr.normalTexturePath = texPath.resolve();
                break;
            case material::TextureSlot::ORM:
                pbr.ormTexturePath = texPath.resolve();
                break;
            case material::TextureSlot::Metallic:
                pbr.metallicTexturePath = texPath.resolve();
                break;
            case material::TextureSlot::Roughness:
                pbr.roughnessTexturePath = texPath.resolve();
                break;
            case material::TextureSlot::AO:
                pbr.aoTexturePath = texPath.resolve();
                break;
            case material::TextureSlot::Emission:
                pbr.emissionTexturePath = texPath.resolve();
                break;
            case material::TextureSlot::Height:
                pbr.heightTexturePath = texPath.resolve();
                break;
            default:
                break;
            }
        }

        return pbr;
    }

    ExtractedPBRValues MaterialPBRExtractor::extractPBRFromPath(
        const std::string& materialOrInstancePath,
        const ParameterOverrides* runtimeOverrides)
    {
        ExtractedPBRValues pbr;

        if (materialOrInstancePath.empty())
        {
            return pbr;
        }

        // Check if this is a material instance
        if (material::isInstanceFile(materialOrInstancePath))
        {
            // Load instance data
            auto instanceData = resource::ResourceManager::loadMaterialInstance(asset::AssetRef::fromPath(materialOrInstancePath));
            if (!instanceData || !instanceData->parentMaterialRef.isValid())
            {
                return pbr;
            }

            // Load parent material
            auto parentMaterial = resource::ResourceManager::loadMaterial(instanceData->parentMaterialRef);
            if (!parentMaterial)
            {
                return pbr;
            }

            // Extract with overrides
            pbr = extractPBRFromInstance(*instanceData, *parentMaterial, runtimeOverrides);
            pbr.materialPath = materialOrInstancePath;
        }
        else
        {
            // Regular material
            auto matData = resource::ResourceManager::loadMaterial(asset::AssetRef::fromPath(materialOrInstancePath));
            if (!matData)
            {
                return pbr;
            }

            if (runtimeOverrides && !runtimeOverrides->empty())
            {
                material::MaterialParameterSet paramSet = material::collectParameters(matData->graph);
                ParameterOverrides resolved = material::resolveOverrides(paramSet, nullptr, runtimeOverrides);
                pbr = extractPBRFromMaterial(*matData, resolved.empty() ? nullptr : &resolved);
            }
            else
            {
                pbr = extractPBRFromMaterial(*matData);
            }
            pbr.materialPath = materialOrInstancePath;
        }

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
