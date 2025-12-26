#pragma once
#include <glm/glm.hpp>
#include "PreviewInstanceId.hpp"
#include <string>
#include <any>

namespace services {

    struct MaterialPreviewParams {
        glm::vec4 albedo{ 0.8f, 0.8f, 0.8f, 1.0f };
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;

        std::string albedoTexturePath;
        std::string normalTexturePath;
        std::string ormTexturePath;          // Packed ORM texture (R=AO, G=Roughness, B=Metallic)
        std::string metallicTexturePath;
        std::string roughnessTexturePath;
        std::string aoTexturePath;
        std::string emissionTexturePath;
        std::string heightTexturePath;

        std::string materialPath;

        bool useCustomShader = false;

        // Type-safe handle to material data for dynamic shader evaluation (Time, Sin, Cos nodes).
        // Expected type: std::shared_ptr<material::MaterialData>
        // Usage: params.materialDataHandle = myMaterialDataSharedPtr;
        std::any materialDataHandle;
    };

    class IMaterialPreviewProvider {
    public:
        virtual ~IMaterialPreviewProvider() = default;

        virtual void initMaterialPreview(PreviewInstanceId instanceId) = 0;

        virtual void cleanUpMaterialPreview(PreviewInstanceId instanceId) = 0;

        virtual bool isMaterialPreviewInitialized(PreviewInstanceId instanceId) const = 0;

        virtual void setMaterialParams(PreviewInstanceId instanceId, const MaterialPreviewParams& params) = 0;

        virtual MaterialPreviewParams getMaterialParams(PreviewInstanceId instanceId) const = 0;

        virtual void updateMaterialCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                          const glm::vec3& cameraPos, float time = 0.0f) = 0;

        virtual void* renderMaterialPreview(PreviewInstanceId instanceId) = 0;

        virtual std::string getMaterialShaderError(PreviewInstanceId instanceId) const = 0;
    };

}
