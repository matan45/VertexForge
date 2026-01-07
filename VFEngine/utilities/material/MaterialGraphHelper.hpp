#pragma once

#include "MaterialTypes.hpp"
#include <glm/glm.hpp>
#include <optional>

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
        static ExtractedParentPBR extractPBRFromGraph(const MaterialData& matData);

    private:
        static std::optional<NodeProperty> getConnectedValue(
            const ShaderGraph& graph,
            uint32_t targetNodeId,
            const std::string& targetPinName);

        static std::string getConnectedTexturePath(
            const ShaderGraph& graph,
            uint32_t targetNodeId,
            const std::string& targetPinName);

        static std::string getOrmTexturePath(
            const ShaderGraph& graph,
            uint32_t targetNodeId);
    };
}
