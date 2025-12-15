#pragma once
#include <glm/glm.hpp>
#include <math/Frustum.hpp>  // For math::AABB (from Utilities)
#include "../data/DTOs.hpp"  // For SubMeshInfo, ViewportTextureHandle
#include <memory>
#include <string>
#include <vector>

namespace services {

    /**
     * @brief Parameters for material preview rendering.
     *
     * This is a service-level DTO that abstracts the Graphics module's
     * PreviewMaterialParams, avoiding direct dependency on material types.
     */
    struct MaterialPreviewParams {
        // Material properties
        glm::vec4 albedo{ 0.8f, 0.8f, 0.8f, 1.0f };
        float metallic = 0.0f;
        float roughness = 0.5f;
        float ao = 1.0f;
        float emission = 0.0f;

        // Texture paths (empty = use scalar value)
        std::string albedoTexturePath;
        std::string metallicTexturePath;
        std::string roughnessTexturePath;
        std::string aoTexturePath;
        std::string normalTexturePath;
        std::string emissionTexturePath;

        // Material path for custom shader pipeline lookup
        std::string materialPath;

        // Flag to enable custom shader pipeline (only after explicit compile)
        bool useCustomShader = false;

        // Opaque handle to material data for dynamic evaluation
        // The provider implementation knows how to interpret this
        void* materialDataHandle = nullptr;
    };

    /**
     * @brief Parameters for mesh preview rendering.
     */
    struct MeshPreviewParams {
        glm::mat4 modelMatrix{ 1.0f };
        int highlightedSubMesh = -1;  // -1 = none highlighted
    };

    /**
     * @brief Provider interface for preview rendering operations.
     *
     * This interface abstracts the Graphics module's MaterialPreviewController
     * and MeshPreviewController, allowing Services to provide preview functionality
     * without depending on Graphics directly.
     * Core/Graphics implements this interface via an adapter class.
     *
     * All methods take an instanceId parameter to support multiple preview instances
     * (e.g., multiple editor windows each with their own preview).
     */
    class IPreviewProvider {
    public:
        virtual ~IPreviewProvider() = default;

        // === Material Preview ===

        /**
         * @brief Initialize material preview renderer for a specific instance.
         * @param instanceId Unique identifier for this preview instance (e.g., window pointer)
         */
        virtual void initMaterialPreview(void* instanceId) = 0;

        /**
         * @brief Clean up material preview renderer for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void cleanUpMaterialPreview(void* instanceId) = 0;

        /**
         * @brief Check if material preview is initialized for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual bool isMaterialPreviewInitialized(void* instanceId) const = 0;

        /**
         * @brief Set material parameters for preview.
         * @param instanceId Unique identifier for this preview instance
         * @param params Material parameters
         */
        virtual void setMaterialParams(void* instanceId, const MaterialPreviewParams& params) = 0;

        /**
         * @brief Get current material parameters.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual MaterialPreviewParams getMaterialParams(void* instanceId) const = 0;

        /**
         * @brief Update material preview camera.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void updateMaterialCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                                          const glm::vec3& cameraPos, float time = 0.0f) = 0;

        /**
         * @brief Render material preview and return ImGui texture handle.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void* renderMaterialPreview(void* instanceId) = 0;

        /**
         * @brief Get last shader compilation error message.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual std::string getMaterialShaderError(void* instanceId) const = 0;

        // === Mesh Preview ===

        /**
         * @brief Initialize mesh preview renderer for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void initMeshPreview(void* instanceId) = 0;

        /**
         * @brief Clean up mesh preview renderer for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void cleanUpMeshPreview(void* instanceId) = 0;

        /**
         * @brief Check if mesh preview is initialized for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual bool isMeshPreviewInitialized(void* instanceId) const = 0;

        /**
         * @brief Load a mesh for preview.
         * @param instanceId Unique identifier for this preview instance
         * @param meshPath Path to the mesh file
         * @param outBounds Output parameter for mesh bounding box
         * @return true if mesh loaded successfully
         */
        virtual bool loadPreviewMesh(void* instanceId, const std::string& meshPath, math::AABB& outBounds) = 0;

        /**
         * @brief Unload the current preview mesh.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void unloadPreviewMesh(void* instanceId) = 0;

        /**
         * @brief Check if a mesh is currently loaded for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual bool isPreviewMeshLoaded(void* instanceId) const = 0;

        /**
         * @brief Get submesh information for the loaded mesh.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(void* instanceId) const = 0;

        /**
         * @brief Get the bounding box of the loaded mesh.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual math::AABB getPreviewMeshBounds(void* instanceId) const = 0;

        /**
         * @brief Set mesh preview parameters.
         * @param instanceId Unique identifier for this preview instance
         * @param params Mesh preview parameters
         */
        virtual void setMeshPreviewParams(void* instanceId, const MeshPreviewParams& params) = 0;

        /**
         * @brief Update mesh preview camera.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void updateMeshCamera(void* instanceId, const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& cameraPos) = 0;

        /**
         * @brief Render mesh preview and return ImGui texture handle.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void* renderMeshPreview(void* instanceId) = 0;
    };

}
