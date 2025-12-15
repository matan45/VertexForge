#pragma once
#include "EventTypes.hpp"
#include "../providers/IPreviewProvider.hpp"
#include "../data/DTOs.hpp"
#include <math/Frustum.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace services::events::preview {

    // ============================================================
    // MATERIAL PREVIEW COMMANDS (Multi-instance support via instanceId)
    // ============================================================

    /**
     * @brief Command to initialize material preview renderer.
     * @param instanceId Unique identifier for this preview instance (e.g., window pointer)
     */
    struct InitMaterialPreviewCommand : ::events::ICommand<void> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "InitMaterialPreview"; }
    };

    /**
     * @brief Command to clean up material preview renderer.
     */
    struct CleanUpMaterialPreviewCommand : ::events::ICommand<void> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "CleanUpMaterialPreview"; }
    };

    /**
     * @brief Command to set material preview parameters.
     */
    struct SetMaterialParamsCommand : ::events::ICommand<void> {
        void* instanceId = nullptr;
        MaterialPreviewParams params;
        std::string_view getName() const override { return "SetMaterialParams"; }
    };

    /**
     * @brief Command to update material preview camera.
     */
    struct UpdateMaterialCameraCommand : ::events::ICommand<void> {
        void* instanceId = nullptr;
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 cameraPos;
        float time = 0.0f;
        std::string_view getName() const override { return "UpdateMaterialCamera"; }
    };

    // ============================================================
    // MATERIAL PREVIEW QUERIES
    // ============================================================

    /**
     * @brief Query to check if material preview is ready.
     */
    struct IsMaterialPreviewReadyQuery : ::events::IQuery<bool> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "IsMaterialPreviewReady"; }
    };

    /**
     * @brief Query to get current material parameters.
     */
    struct GetMaterialParamsQuery : ::events::IQuery<MaterialPreviewParams> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "GetMaterialParams"; }
    };

    /**
     * @brief Query to render material preview and get texture.
     */
    struct RenderMaterialPreviewQuery : ::events::IQuery<ViewportTextureHandle> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "RenderMaterialPreview"; }
    };

    /**
     * @brief Query to get last shader compilation error.
     */
    struct GetMaterialShaderErrorQuery : ::events::IQuery<std::string> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "GetMaterialShaderError"; }
    };

    // ============================================================
    // MESH PREVIEW COMMANDS (Multi-instance support via instanceId)
    // ============================================================

    /**
     * @brief Command to initialize mesh preview renderer.
     */
    struct InitMeshPreviewCommand : ::events::ICommand<void> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "InitMeshPreview"; }
    };

    /**
     * @brief Command to clean up mesh preview renderer.
     */
    struct CleanUpMeshPreviewCommand : ::events::ICommand<void> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "CleanUpMeshPreview"; }
    };

    /**
     * @brief Result of LoadPreviewMeshCommand.
     */
    struct LoadPreviewMeshResult {
        bool success = false;
        math::AABB bounds;
    };

    /**
     * @brief Command to load a mesh for preview.
     */
    struct LoadPreviewMeshCommand : ::events::ICommand<LoadPreviewMeshResult> {
        void* instanceId = nullptr;
        std::string meshPath;
        std::string_view getName() const override { return "LoadPreviewMesh"; }
    };

    /**
     * @brief Command to unload the current preview mesh.
     */
    struct UnloadPreviewMeshCommand : ::events::ICommand<void> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "UnloadPreviewMesh"; }
    };

    /**
     * @brief Command to set mesh preview parameters.
     */
    struct SetMeshPreviewParamsCommand : ::events::ICommand<void> {
        void* instanceId = nullptr;
        MeshPreviewParams params;
        std::string_view getName() const override { return "SetMeshPreviewParams"; }
    };

    /**
     * @brief Command to update mesh preview camera.
     */
    struct UpdateMeshCameraCommand : ::events::ICommand<void> {
        void* instanceId = nullptr;
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 cameraPos;
        std::string_view getName() const override { return "UpdateMeshCamera"; }
    };

    // ============================================================
    // MESH PREVIEW QUERIES
    // ============================================================

    /**
     * @brief Query to check if mesh preview is ready.
     */
    struct IsMeshPreviewReadyQuery : ::events::IQuery<bool> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "IsMeshPreviewReady"; }
    };

    /**
     * @brief Query to check if a mesh is loaded for preview.
     */
    struct IsPreviewMeshLoadedQuery : ::events::IQuery<bool> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "IsPreviewMeshLoaded"; }
    };

    /**
     * @brief Query to get submesh information.
     */
    struct GetPreviewMeshSubMeshInfoQuery : ::events::IQuery<std::vector<SubMeshInfo>> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "GetPreviewMeshSubMeshInfo"; }
    };

    /**
     * @brief Query to get preview mesh bounds.
     */
    struct GetPreviewMeshBoundsQuery : ::events::IQuery<math::AABB> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "GetPreviewMeshBounds"; }
    };

    /**
     * @brief Query to render mesh preview and get texture.
     */
    struct RenderMeshPreviewQuery : ::events::IQuery<ViewportTextureHandle> {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "RenderMeshPreview"; }
    };

    // ============================================================
    // NOTIFICATIONS
    // ============================================================

    /**
     * @brief Notification when material preview is initialized.
     */
    struct MaterialPreviewInitializedNotification : ::events::INotification {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "MaterialPreviewInitialized"; }
    };

    /**
     * @brief Notification when mesh preview is initialized.
     */
    struct MeshPreviewInitializedNotification : ::events::INotification {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "MeshPreviewInitialized"; }
    };

    /**
     * @brief Notification when a preview mesh is loaded.
     */
    struct PreviewMeshLoadedNotification : ::events::INotification {
        void* instanceId = nullptr;
        std::string meshPath;
        math::AABB bounds;
        std::string_view getName() const override { return "PreviewMeshLoaded"; }
    };

    /**
     * @brief Notification when preview mesh is unloaded.
     */
    struct PreviewMeshUnloadedNotification : ::events::INotification {
        void* instanceId = nullptr;
        std::string_view getName() const override { return "PreviewMeshUnloaded"; }
    };

}
