#pragma once
#include "../EventTypes.hpp"
#include "../../providers/render/IPreviewProvider.hpp"
#include "../../data/DTOs.hpp"
#include "../../data/AsyncLoadingTypes.hpp"
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

    struct SetMeshPreviewParamsCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        MeshPreviewParams params;
        std::string_view getName() const override { return "SetMeshPreviewParams"; }
    };

    struct SetPreviewEnvironmentCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        PreviewEnvironmentParams params;
        std::string_view getName() const override { return "SetPreviewEnvironment"; }
    };

   
    struct UpdateMeshCameraCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 cameraPos;
        std::string_view getName() const override { return "UpdateMeshCamera"; }
    };

    // ============================================================
    // ASYNC MESH LOADING COMMANDS
    // ============================================================

    struct LoadPreviewMeshAsyncCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        std::string meshPath;
        std::string_view getName() const override { return "LoadPreviewMeshAsync"; }
    };

    struct CancelMeshLoadingCommand : ::events::ICommand<void> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "CancelMeshLoading"; }
    };

    // ============================================================
    // ASYNC MESH LOADING QUERIES
    // ============================================================

    struct GetMeshLoadingProgressQuery : ::events::IQuery<MeshLoadingProgress> {
        PreviewInstanceId instanceId;
        std::string_view getName() const override { return "GetMeshLoadingProgress"; }
    };

    // ============================================================
    // MESH PREVIEW QUERIES
    // ============================================================

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

}
