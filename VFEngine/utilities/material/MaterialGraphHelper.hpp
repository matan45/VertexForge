#pragma once

#include "MaterialTypes.hpp"
#include <glm/glm.hpp>
#include <map>
#include <optional>
#include <string>

namespace material
{
    struct ExtractedParentPBR
    {
        glm::vec4 albedo{1.0f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;
        float iblDiffuse = 1.0f;
        float iblSpecular = 0.5f;

        std::string albedoTexturePath;
        std::string normalTexturePath;
        std::string ormTexturePath;
        std::string metallicTexturePath;
        std::string roughnessTexturePath;
        std::string aoTexturePath;
        std::string emissionTexturePath;
        std::string heightTexturePath;
    };

    class MaterialGraphHelper
    {
    public:
        // paramOverrides: resolved named-parameter values (see MaterialParameterSet's
        // resolveOverrides) consulted when the walk hits an exposed parameter node
        static ExtractedParentPBR extractPBRFromGraph(
            const MaterialData& matData,
            const std::map<std::string, ParameterValue>* paramOverrides = nullptr);

    private:
        static std::optional<NodeProperty> getConnectedValue(
            const ShaderGraph& graph,
            uint32_t targetNodeId,
            const std::string& targetPinName,
            const std::map<std::string, ParameterValue>* paramOverrides);

        static std::string getConnectedTexturePath(
            const ShaderGraph& graph,
            uint32_t targetNodeId,
            const std::string& targetPinName);

        static std::string getOrmTexturePath(
            const ShaderGraph& graph,
            uint32_t targetNodeId);
    };
}
