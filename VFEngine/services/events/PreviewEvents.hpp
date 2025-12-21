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
    
    struct InitMaterialPreviewCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "InitMaterialPreview"; }
    };

    
    struct CleanUpMaterialPreviewCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "CleanUpMaterialPreview"; }
    };

    
    struct SetMaterialParamsCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        MaterialPreviewParams params;
        std::string_view getName() const override { return "SetMaterialParams"; }
    };

   
    struct UpdateMaterialCameraCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 cameraPos;
        float time = 0.0f;
        std::string_view getName() const override { return "UpdateMaterialCamera"; }
    };

    // ============================================================
    // MATERIAL PREVIEW QUERIES
    // ============================================================

    
    struct IsMaterialPreviewReadyQuery : ::events::IQuery<bool> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "IsMaterialPreviewReady"; }
    };

    
    struct GetMaterialParamsQuery : ::events::IQuery<MaterialPreviewParams> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetMaterialParams"; }
    };

    
    struct RenderMaterialPreviewQuery : ::events::IQuery<ViewportTextureHandle> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "RenderMaterialPreview"; }
    };

    
    struct GetMaterialShaderErrorQuery : ::events::IQuery<std::string> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetMaterialShaderError"; }
    };

    // ============================================================
    // MESH PREVIEW COMMANDS (Multi-instance support via instanceId)
    // ============================================================

   
    struct InitMeshPreviewCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "InitMeshPreview"; }
    };

    
    struct CleanUpMeshPreviewCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "CleanUpMeshPreview"; }
    };

   
    struct LoadPreviewMeshResult {
        bool success = false;
        math::AABB bounds;
    };

   
    struct LoadPreviewMeshCommand : ::events::ICommand<LoadPreviewMeshResult> {
        PreviewInstanceId instanceId;
        std::string meshPath;
        std::string_view getName() const override { return "LoadPreviewMesh"; }
    };

    
    struct UnloadPreviewMeshCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "UnloadPreviewMesh"; }
    };

   
    struct SetMeshPreviewParamsCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        MeshPreviewParams params;
        std::string_view getName() const override { return "SetMeshPreviewParams"; }
    };

   
    struct UpdateMeshCameraCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 cameraPos;
        std::string_view getName() const override { return "UpdateMeshCamera"; }
    };

    // ============================================================
    // MESH PREVIEW QUERIES
    // ============================================================

   
    struct IsMeshPreviewReadyQuery : ::events::IQuery<bool> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "IsMeshPreviewReady"; }
    };

    
    struct IsPreviewMeshLoadedQuery : ::events::IQuery<bool> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "IsPreviewMeshLoaded"; }
    };


    struct GetPreviewMeshSubMeshInfoQuery : ::events::IQuery<std::vector<SubMeshInfo>> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetPreviewMeshSubMeshInfo"; }
    };


    struct GetPreviewMeshLODInfoQuery : ::events::IQuery<std::vector<LODInfo>> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetPreviewMeshLODInfo"; }
    };


    struct GetPreviewMeshBoundsQuery : ::events::IQuery<math::AABB> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetPreviewMeshBounds"; }
    };

   
    struct RenderMeshPreviewQuery : ::events::IQuery<ViewportTextureHandle> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "RenderMeshPreview"; }
    };

    // ============================================================
    // NOTIFICATIONS
    // ============================================================

    
    struct MaterialPreviewInitializedNotification : ::events::INotification {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "MaterialPreviewInitialized"; }
    };

    
    struct MeshPreviewInitializedNotification : ::events::INotification {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "MeshPreviewInitialized"; }
    };

    
    struct PreviewMeshLoadedNotification : ::events::INotification {
        PreviewInstanceId instanceId;
        std::string meshPath;
        math::AABB bounds;
        std::string_view getName() const override { return "PreviewMeshLoaded"; }
    };

    
    struct PreviewMeshUnloadedNotification : ::events::INotification {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "PreviewMeshUnloaded"; }
    };

}
