#pragma once
#include <material/MaterialTypes.hpp>
#include <providers/IMaterialPreviewProvider.hpp>
#include <optional>
#include <string>
#include <memory>
#include <glm/glm.hpp>

namespace editor::materialeditor
{
    struct GraphEvaluationResult
    {
        std::optional<glm::vec4> albedo;
        std::optional<float> metallic;
        std::optional<float> roughness;
        std::optional<float> ao;
        std::optional<float> emission;

        std::string albedoTexturePath;
        std::string normalTexturePath;
        std::string emissionTexturePath;
        std::string heightTexturePath;
        std::string ormTexturePath;
        std::string metallicTexturePath;
        std::string roughnessTexturePath;
        std::string aoTexturePath;
    };

    class MaterialGraphEvaluator
    {
    public:
        static GraphEvaluationResult evaluate(const ::material::ShaderGraph& graph);

        static services::MaterialPreviewParams toPreviewParams(
            const GraphEvaluationResult& result,
            const std::string& materialPath,
            std::shared_ptr<::material::MaterialData> materialData,
            bool useCustomShader);

    private:
        static std::optional<float> getConnectedScalar(
            const ::material::ShaderGraph& graph,
            const ::material::ShaderNode* pbrOutput,
            const std::string& pinName);

        static std::optional<glm::vec4> getConnectedColor(
            const ::material::ShaderGraph& graph,
            const ::material::ShaderNode* pbrOutput,
            const std::string& pinName);

        static std::string getConnectedTexturePath(
            const ::material::ShaderGraph& graph,
            const ::material::ShaderNode* pbrOutput,
            const std::string& pinName);

        static std::string getOrmTexturePath(
            const ::material::ShaderGraph& graph,
            const ::material::ShaderNode* pbrOutput);

        static std::string cleanPath(const std::string& path);
    };
}
