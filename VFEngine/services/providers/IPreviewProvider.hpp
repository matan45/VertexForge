#pragma once
#include <glm/glm.hpp>
#include <math/Frustum.hpp>
#include "../data/DTOs.hpp"
#include "PreviewInstanceId.hpp"
#include <string>
#include <vector>
#include <any>

namespace services {

    
    struct MaterialPreviewParams {
        glm::vec4 albedo{ 0.8f, 0.8f, 0.8f, 1.0f };
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;
        
        std::string albedoTexturePath;
        std::string metallicTexturePath;
        std::string roughnessTexturePath;
        std::string aoTexturePath;
        std::string normalTexturePath;
        std::string emissionTexturePath;
        
        std::string materialPath;
        
        bool useCustomShader = false;

        // Type-safe handle to material data for dynamic shader evaluation (Time, Sin, Cos nodes).
        // Expected type: std::shared_ptr<material::MaterialData>
        // Usage: params.materialDataHandle = myMaterialDataSharedPtr;
        std::any materialDataHandle;
    };

    
    struct MeshPreviewParams {
        glm::mat4 modelMatrix{ 1.0f };
        int highlightedSubMesh = -1;  // -1 = none highlighted
    };

   
    class IPreviewProvider {
    public:
        virtual ~IPreviewProvider() = default;

        // === Material Preview ===

        virtual void initMaterialPreview(PreviewInstanceId instanceId) = 0;

        virtual void cleanUpMaterialPreview(PreviewInstanceId instanceId) = 0;

        virtual bool isMaterialPreviewInitialized(PreviewInstanceId instanceId) const = 0;

        virtual void setMaterialParams(PreviewInstanceId instanceId, const MaterialPreviewParams& params) = 0;

        virtual MaterialPreviewParams getMaterialParams(PreviewInstanceId instanceId) const = 0;

        virtual void updateMaterialCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                          const glm::vec3& cameraPos, float time = 0.0f) = 0;

        virtual void* renderMaterialPreview(PreviewInstanceId instanceId) = 0;

        virtual std::string getMaterialShaderError(PreviewInstanceId instanceId) const = 0;

        // === Mesh Preview ===

        virtual void initMeshPreview(PreviewInstanceId instanceId) = 0;

        virtual void cleanUpMeshPreview(PreviewInstanceId instanceId) = 0;

        virtual bool isMeshPreviewInitialized(PreviewInstanceId instanceId) const = 0;

        virtual bool loadPreviewMesh(PreviewInstanceId instanceId, const std::string& meshPath, math::AABB& outBounds) = 0;

        virtual void unloadPreviewMesh(PreviewInstanceId instanceId) = 0;

        virtual bool isPreviewMeshLoaded(PreviewInstanceId instanceId) const = 0;

        virtual std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const = 0;

        virtual math::AABB getPreviewMeshBounds(PreviewInstanceId instanceId) const = 0;

        virtual void setMeshPreviewParams(PreviewInstanceId instanceId, const MeshPreviewParams& params) = 0;

        virtual void updateMeshCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& cameraPos) = 0;

        virtual void* renderMeshPreview(PreviewInstanceId instanceId) = 0;
    };

}
