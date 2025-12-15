#pragma once
#include <glm/glm.hpp>
#include <math/Frustum.hpp>
#include "../data/DTOs.hpp" 
#include <string>
#include <vector>

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

        // Opaque handle to material data for dynamic evaluation
        // The provider implementation knows how to interpret this
        void* materialDataHandle = nullptr;
    };

    
    struct MeshPreviewParams {
        glm::mat4 modelMatrix{ 1.0f };
        int highlightedSubMesh = -1;  // -1 = none highlighted
    };

   
    class IPreviewProvider {
    public:
        virtual ~IPreviewProvider() = default;

        // === Material Preview ===
        
        virtual void initMaterialPreview(void* instanceId) = 0;
        
        virtual void cleanUpMaterialPreview(void* instanceId) = 0;
        
        virtual bool isMaterialPreviewInitialized(void* instanceId) const = 0;
        
        virtual void setMaterialParams(void* instanceId, const MaterialPreviewParams& params) = 0;
        
        virtual MaterialPreviewParams getMaterialParams(void* instanceId) const = 0;
        
        virtual void updateMaterialCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                                          const glm::vec3& cameraPos, float time = 0.0f) = 0;
        
        virtual void* renderMaterialPreview(void* instanceId) = 0;
        
        virtual std::string getMaterialShaderError(void* instanceId) const = 0;

        // === Mesh Preview ===
        
        virtual void initMeshPreview(void* instanceId) = 0;
        
        virtual void cleanUpMeshPreview(void* instanceId) = 0;
        
        virtual bool isMeshPreviewInitialized(void* instanceId) const = 0;
        
        virtual bool loadPreviewMesh(void* instanceId, const std::string& meshPath, math::AABB& outBounds) = 0;
        
        virtual void unloadPreviewMesh(void* instanceId) = 0;
        
        virtual bool isPreviewMeshLoaded(void* instanceId) const = 0;
        
        virtual std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(void* instanceId) const = 0;
        
        virtual math::AABB getPreviewMeshBounds(void* instanceId) const = 0;
        
        virtual void setMeshPreviewParams(void* instanceId, const MeshPreviewParams& params) = 0;
        
        virtual void updateMeshCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& cameraPos) = 0;
        
        virtual void* renderMeshPreview(void* instanceId) = 0;
    };

}
