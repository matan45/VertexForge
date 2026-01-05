#pragma once
#include "EventTypes.hpp"
#include "../data/DTOs.hpp"
#include "../data/AsyncLoadingTypes.hpp"
#include "../providers/IOffScreenProvider.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <string>

namespace events::render {

    // ============================================
    // COMMANDS - Operations that modify render state
    // ============================================

    struct SetIBLCommand : ICommand<bool> {
        std::string hdrPath;

        std::string_view getName() const override { return "SetIBL"; }
    };

    struct RemoveIBLCommand : ICommand<> {
        std::string_view getName() const override { return "RemoveIBL"; }
    };

    struct RemoveCameraCommand : ICommand<> {
        services::CameraId cameraId;

        std::string_view getName() const override { return "RemoveCamera"; }
    };

    struct UpdateIBLCameraCommand : ICommand<> {
        glm::mat4 viewMatrix;
        glm::mat4 projectionMatrix;

        std::string_view getName() const override { return "UpdateIBLCamera"; }
    };

    struct LoadEditorTextureCommand : ICommand<services::EditorTextureHandle> {
        std::string path;

        std::string_view getName() const override { return "LoadEditorTexture"; }
    };

    struct ReleaseEditorTextureCommand : ICommand<> {
        void* handle;

        std::string_view getName() const override { return "ReleaseEditorTexture"; }
    };

    struct LoadEditorTextureAsyncCommand : ICommand<> {
        void* instanceId;
        std::string path;
        bool isHDR = false;

        std::string_view getName() const override { return "LoadEditorTextureAsync"; }
    };

    struct CancelTextureLoadingCommand : ICommand<> {
        void* instanceId;

        std::string_view getName() const override { return "CancelTextureLoading"; }
    };

    struct UpdateMeshCameraCommand : ICommand<> {
        glm::mat4 viewMatrix;
        glm::mat4 projectionMatrix;
        glm::vec3 cameraPosition;
        float time = 0.0f;

        std::string_view getName() const override { return "UpdateMeshCamera"; }
    };

    struct SetShowBillboardIconsCommand : ICommand<> {
        bool show;

        std::string_view getName() const override { return "SetShowBillboardIcons"; }
    };

    struct SetShowDebugRenderingCommand : ICommand<> {
        bool show;

        std::string_view getName() const override { return "SetShowDebugRendering"; }
    };

    struct SetShowGridCommand : ICommand<> {
        bool show;

        std::string_view getName() const override { return "SetShowGrid"; }
    };

    struct LoadBillboardAtlasCommand : ICommand<bool> {
        std::string atlasPath;

        std::string_view getName() const override { return "LoadBillboardAtlas"; }
    };

    // ============================================
    // QUERIES - Read-only operations
    // ============================================

    struct GetTextureLoadingProgressQuery : IQuery<services::TextureLoadingProgress> {
        void* instanceId;

        std::string_view getName() const override { return "GetTextureLoadingProgress"; }
    };

    struct GetLoadedTextureHandleQuery : IQuery<services::EditorTextureHandle> {
        void* instanceId;

        std::string_view getName() const override { return "GetLoadedTextureHandle"; }
    };

    struct GetViewportTextureQuery : IQuery<services::ViewportTextureHandle> {
        std::string_view getName() const override { return "GetViewportTexture"; }
    };

    struct GetMeshBoundingBoxQuery : IQuery<std::optional<services::MeshBoundingBox>> {
        std::string meshPath;

        std::string_view getName() const override { return "GetMeshBoundingBox"; }
    };

    struct GetShowBillboardIconsQuery : IQuery<bool> {
        std::string_view getName() const override { return "GetShowBillboardIcons"; }
    };

    struct GetShowDebugRenderingQuery : IQuery<bool> {
        std::string_view getName() const override { return "GetShowDebugRendering"; }
    };

    struct GetShowGridQuery : IQuery<bool> {
        std::string_view getName() const override { return "GetShowGrid"; }
    };

    struct GetCullingStatsQuery : IQuery<services::CullingDebugStats> {
        std::string_view getName() const override { return "GetCullingStats"; }
    };

}
