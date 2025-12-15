#pragma once
#include "../providers/IPreviewProvider.hpp"
#include "../data/DTOs.hpp"
#include <math/Frustum.hpp>
#include <string>
#include <vector>

namespace services {

    /**
     * @brief Service interface for material and mesh preview operations.
     *
     * This service provides high-level APIs for Editor windows to render
     * material and mesh previews without direct access to Graphics controllers.
     *
     * All methods take an instanceId parameter to support multiple preview instances
     * (e.g., multiple editor windows each with their own preview).
     */
    class IPreviewService {
    public:
        virtual ~IPreviewService() = default;

        // === Material Preview ===

        /**
         * @brief Initialize the material preview renderer for a specific instance.
         * @param instanceId Unique identifier for this preview instance (e.g., window pointer)
         */
        virtual void initMaterialPreview(PreviewInstanceId instanceId) = 0;

        /**
         * @brief Clean up the material preview renderer for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void cleanUpMaterialPreview(PreviewInstanceId instanceId) = 0;

        /**
         * @brief Check if material preview is ready for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        [[nodiscard]] virtual bool isMaterialPreviewReady(PreviewInstanceId instanceId) const = 0;

        /**
         * @brief Set material preview parameters for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         * @param params Material parameters
         */
        virtual void setMaterialParams(PreviewInstanceId instanceId, const MaterialPreviewParams& params) = 0;

        /**
         * @brief Get current material preview parameters for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        [[nodiscard]] virtual MaterialPreviewParams getMaterialParams(PreviewInstanceId instanceId) const = 0;

        /**
         * @brief Update material preview camera for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void updateMaterialCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                          const glm::vec3& cameraPos, float time = 0.0f) = 0;

        /**
         * @brief Render material preview and return the texture handle for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        [[nodiscard]] virtual ViewportTextureHandle renderMaterialPreview(PreviewInstanceId instanceId) = 0;

        /**
         * @brief Get the last shader compilation error message for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        [[nodiscard]] virtual std::string getMaterialShaderError(PreviewInstanceId instanceId) const = 0;

        // === Mesh Preview ===

        /**
         * @brief Initialize the mesh preview renderer for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void initMeshPreview(PreviewInstanceId instanceId) = 0;

        /**
         * @brief Clean up the mesh preview renderer for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void cleanUpMeshPreview(PreviewInstanceId instanceId) = 0;

        /**
         * @brief Check if mesh preview is ready for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        [[nodiscard]] virtual bool isMeshPreviewReady(PreviewInstanceId instanceId) const = 0;

        /**
         * @brief Load a mesh for preview.
         * @param instanceId Unique identifier for this preview instance
         * @param meshPath Path to the mesh file
         * @param outBounds Output parameter for mesh bounding box
         * @return true if mesh loaded successfully
         */
        [[nodiscard]] virtual bool loadPreviewMesh(PreviewInstanceId instanceId, const std::string& meshPath, math::AABB& outBounds) = 0;

        /**
         * @brief Unload the current preview mesh for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void unloadPreviewMesh(PreviewInstanceId instanceId) = 0;

        /**
         * @brief Check if a mesh is currently loaded for preview.
         * @param instanceId Unique identifier for this preview instance
         */
        [[nodiscard]] virtual bool isPreviewMeshLoaded(PreviewInstanceId instanceId) const = 0;

        /**
         * @brief Get submesh information for the loaded mesh.
         * @param instanceId Unique identifier for this preview instance
         */
        [[nodiscard]] virtual std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo(PreviewInstanceId instanceId) const = 0;

        /**
         * @brief Get the bounding box of the loaded preview mesh.
         * @param instanceId Unique identifier for this preview instance
         */
        [[nodiscard]] virtual math::AABB getPreviewMeshBounds(PreviewInstanceId instanceId) const = 0;

        /**
         * @brief Set mesh preview parameters for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         * @param params Mesh preview parameters
         */
        virtual void setMeshPreviewParams(PreviewInstanceId instanceId, const MeshPreviewParams& params) = 0;

        /**
         * @brief Update mesh preview camera for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        virtual void updateMeshCamera(PreviewInstanceId instanceId, const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& cameraPos) = 0;

        /**
         * @brief Render mesh preview and return the texture handle for a specific instance.
         * @param instanceId Unique identifier for this preview instance
         */
        [[nodiscard]] virtual ViewportTextureHandle renderMeshPreview(PreviewInstanceId instanceId) = 0;
    };

}
