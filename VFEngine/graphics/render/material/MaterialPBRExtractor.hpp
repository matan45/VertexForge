#pragma once

#include "../mesh/MeshTypes.hpp"
#include "material/MaterialTypes.hpp"
#include "material/MaterialInstanceTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <memory>
#include <optional>

namespace render::mesh
{
    struct ExtractedPBRValues
    {
        glm::vec4 albedo{1.0f, 1.0f, 1.0f, 1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;
        material::BlendMode blendMode = material::BlendMode::Opaque;
        float iblDiffuse = 1.0f;
        float iblSpecular = 0.5f;

        // Texture paths (empty = use scalar value)
        std::string albedoTexturePath;
        std::string normalTexturePath;
        std::string ormTexturePath; // ORM packed texture (R=AO, G=Roughness, B=Metallic)
        std::string metallicTexturePath;
        std::string roughnessTexturePath;
        std::string aoTexturePath;
        std::string emissionTexturePath;
        std::string heightTexturePath;

        bool usesORM() const { return !ormTexturePath.empty(); }

        std::string materialPath;
    };


    class MaterialPBRExtractor
    {
    public:
        static ExtractedPBRValues extractPBRFromMaterial(const material::MaterialData& matData);

        // Extract PBR from material instance (applies overrides to parent values)
        static ExtractedPBRValues extractPBRFromInstance(
            const material::MaterialInstanceData& instance,
            const material::MaterialData& parentMaterial);

        // Unified extraction - handles both .vfMat and .vfMatInstance paths
        static ExtractedPBRValues extractPBRFromPath(const std::string& materialOrInstancePath);

        static ExtractedPBRValues getPBRForSubmesh(
            const MeshRenderData& meshData,
            const std::string& submeshName,
            const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& matCache,
            float time = 0.0f);

        static float evaluateEmissionStrength(
            const material::ShaderGraph& graph,
            uint32_t outputNodeId,
            float time);

    private:
        static std::optional<float> evaluateFloatValue(
            const material::ShaderGraph& graph,
            uint32_t nodeId,
            float time);

        static std::optional<float> getInputFloat(
            const material::ShaderGraph& graph,
            uint32_t nodeId,
            const std::string& pinName,
            float time);

        static std::optional<material::NodeProperty> getConnectedValue(
            const material::ShaderGraph& graph,
            uint32_t targetNodeId,
            const std::string& targetPinName);

        static std::string getConnectedTexturePath(
            const material::ShaderGraph& graph,
            uint32_t targetNodeId,
            const std::string& targetPinName);
    };
}
