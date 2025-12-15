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
     */
    class IPreviewService {
    public:
        virtual ~IPreviewService() = default;

        // === Material Preview ===

        /**
         * @brief Initialize the material preview renderer.
         */
        virtual void initMaterialPreview() = 0;

        /**
         * @brief Clean up the material preview renderer.
         */
        virtual void cleanUpMaterialPreview() = 0;

        /**
         * @brief Check if material preview is ready.
         */
        virtual bool isMaterialPreviewReady() const = 0;

        /**
         * @brief Set material preview parameters.
         */
        virtual void setMaterialParams(const MaterialPreviewParams& params) = 0;

        /**
         * @brief Get current material preview parameters.
         */
        virtual MaterialPreviewParams getMaterialParams() const = 0;

        /**
         * @brief Update material preview camera.
         */
        virtual void updateMaterialCamera(const glm::mat4& view, const glm::mat4& projection,
                                          const glm::vec3& cameraPos, float time = 0.0f) = 0;

        /**
         * @brief Render material preview and return the texture handle.
         */
        virtual ViewportTextureHandle renderMaterialPreview() = 0;

        /**
         * @brief Get the last shader compilation error message.
         */
        virtual std::string getMaterialShaderError() const = 0;

        // === Mesh Preview ===

        /**
         * @brief Initialize the mesh preview renderer.
         */
        virtual void initMeshPreview() = 0;

        /**
         * @brief Clean up the mesh preview renderer.
         */
        virtual void cleanUpMeshPreview() = 0;

        /**
         * @brief Check if mesh preview is ready.
         */
        virtual bool isMeshPreviewReady() const = 0;

        /**
         * @brief Load a mesh for preview.
         * @param meshPath Path to the mesh file
         * @param outBounds Output parameter for mesh bounding box
         * @return true if mesh loaded successfully
         */
        virtual bool loadPreviewMesh(const std::string& meshPath, math::AABB& outBounds) = 0;

        /**
         * @brief Unload the current preview mesh.
         */
        virtual void unloadPreviewMesh() = 0;

        /**
         * @brief Check if a mesh is currently loaded for preview.
         */
        virtual bool isPreviewMeshLoaded() const = 0;

        /**
         * @brief Get submesh information for the loaded mesh.
         */
        virtual std::vector<SubMeshInfo> getPreviewMeshSubMeshInfo() const = 0;

        /**
         * @brief Get the bounding box of the loaded preview mesh.
         */
        virtual math::AABB getPreviewMeshBounds() const = 0;

        /**
         * @brief Set mesh preview parameters.
         */
        virtual void setMeshPreviewParams(const MeshPreviewParams& params) = 0;

        /**
         * @brief Update mesh preview camera.
         */
        virtual void updateMeshCamera(const glm::mat4& view, const glm::mat4& projection,
                                       const glm::vec3& cameraPos) = 0;

        /**
         * @brief Render mesh preview and return the texture handle.
         */
        virtual ViewportTextureHandle renderMeshPreview() = 0;
    };

}
