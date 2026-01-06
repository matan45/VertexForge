#pragma once

#include "MaterialTypes.hpp"
#include <glm/glm.hpp>
#include <optional>

namespace material
{
    /**
     * Helper to extract PBR values from a material's shader graph.
     * This is in Utilities so Editor can access it (unlike MaterialPBRExtractor in Graphics).
     */
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
        /**
         * Extract PBR values from a material's shader graph by examining
         * what's connected to the PBROutput node's input pins.
         */
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
