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
        float opacity = 1.0f;
        float alphaCutoff = 0.5f;
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
        using ParameterOverrides = std::map<std::string, material::ParameterValue>;

        // paramOverrides: resolved named-parameter values (instance/runtime) consulted
        // when the graph walk hits an exposed parameter node — see MaterialParameterSet.
        static ExtractedPBRValues extractPBRFromMaterial(
            const material::MaterialData& matData,
            const ParameterOverrides* paramOverrides = nullptr);

        // Extract PBR from material instance (applies named parameter overrides during
        // the graph walk, then fixed PBR scalar + texture slot overrides on top).
        // runtimeOverrides win over the instance's own parameter overrides.
        static ExtractedPBRValues extractPBRFromInstance(
            const material::MaterialInstanceData& instance,
            const material::MaterialData& parentMaterial,
            const ParameterOverrides* runtimeOverrides = nullptr);

        // Unified extraction - handles both .vfMat and .vfMatInstance paths.
        // runtimeOverrides: per-entity named-parameter values (MaterialComponent).
        static ExtractedPBRValues extractPBRFromPath(
            const std::string& materialOrInstancePath,
            const ParameterOverrides* runtimeOverrides = nullptr);

        static ExtractedPBRValues getPBRForSubmesh(
            const MeshRenderData& meshData,
            const std::string& submeshName,
            const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& matCache,
            float time = 0.0f);

        static float evaluateEmissionStrength(
            const material::ShaderGraph& graph,
            uint32_t outputNodeId,
            float time,
            const ParameterOverrides* paramOverrides = nullptr);

    private:
        static std::optional<float> evaluateFloatValue(
            const material::ShaderGraph& graph,
            uint32_t nodeId,
            float time,
            const ParameterOverrides* paramOverrides);

        static std::optional<float> getInputFloat(
            const material::ShaderGraph& graph,
            uint32_t nodeId,
            const std::string& pinName,
            float time,
            const ParameterOverrides* paramOverrides);

        static std::optional<material::NodeProperty> getConnectedValue(
            const material::ShaderGraph& graph,
            uint32_t targetNodeId,
            const std::string& targetPinName,
            const ParameterOverrides* paramOverrides);

        static std::string getConnectedTexturePath(
            const material::ShaderGraph& graph,
            uint32_t targetNodeId,
            const std::string& targetPinName);
    };
}
