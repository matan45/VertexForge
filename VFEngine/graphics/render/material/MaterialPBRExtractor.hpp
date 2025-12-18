#pragma once

#include "../mesh/MeshTypes.hpp"
#include "material/MaterialTypes.hpp"
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <memory>
#include <optional>

namespace render::mesh
{
    // Helper struct to hold extracted PBR values from a material
    struct ExtractedPBRValues
    {
        glm::vec4 albedo{ 1.0f, 1.0f, 1.0f, 1.0f };
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;
        material::BlendMode blendMode = material::BlendMode::Opaque;
        float iblDiffuse = 1.0f;
        float iblSpecular = 0.5f;

        // Texture paths (empty = use scalar value)
        std::string albedoTexturePath;
        std::string metallicTexturePath;
        std::string roughnessTexturePath;
        std::string aoTexturePath;
        std::string normalTexturePath;
        std::string emissionTexturePath;

        // Material path for shader cache lookup
        std::string materialPath;
    };

    // Utility class for extracting PBR values from material shader graphs
    class MaterialPBRExtractor
    {
    public:
        // Extract PBR values from a loaded MaterialData by traversing the shader graph
        static ExtractedPBRValues extractPBRFromMaterial(const material::MaterialData& matData);

        // Get PBR values for a submesh, checking material assignments in order:
        // 1. Per-submesh material override
        // 2. Default material for the mesh
        // 3. Fallback defaults from MeshRenderData
        // Note: This function only reads from the cache. Materials must be loaded beforehand.
        static ExtractedPBRValues getPBRForSubmesh(
            const MeshRenderData& meshData,
            const std::string& submeshName,
            const std::unordered_map<std::string, std::shared_ptr<material::MaterialData>>& matCache,
            float time = 0.0f);

        // Evaluate dynamic emission strength (supports Time, Sin, Cos nodes)
        static float evaluateEmissionStrength(
            const material::ShaderGraph& graph,
            uint32_t outputNodeId,
            float time);

    private:
        // Helper to evaluate a node and return its float output value
        static std::optional<float> evaluateFloatValue(
            const material::ShaderGraph& graph,
            uint32_t nodeId,
            const std::string& pinName,
            float time);

        // Helper to get the input value for a node's pin (recursively evaluates connected nodes)
        static std::optional<float> getInputFloat(
            const material::ShaderGraph& graph,
            uint32_t nodeId,
            const std::string& pinName,
            float time);

        // Helper to get value from a node connected to a specific pin (static values only)
        static std::optional<material::NodeProperty> getConnectedValue(
            const material::ShaderGraph& graph,
            uint32_t targetNodeId,
            const std::string& targetPinName);

        // Helper to get texture path from a TextureSample node connected to a specific pin
        static std::string getConnectedTexturePath(
            const material::ShaderGraph& graph,
            uint32_t targetNodeId,
            const std::string& targetPinName);
    };
}
