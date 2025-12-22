#pragma once
#include "EventTypes.hpp"
#include "../data/DTOs.hpp"
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

    // Update IBL camera matrices (called each frame from ViewPort with EditorCamera matrices)
    struct UpdateIBLCameraCommand : ICommand<> {
        glm::mat4 viewMatrix;
        glm::mat4 projectionMatrix;

        std::string_view getName() const override { return "UpdateIBLCamera"; }
    };

    struct ResizeViewportCommand : ICommand<> {
        uint32_t width;
        uint32_t height;

        std::string_view getName() const override { return "ResizeViewport"; }
    };

    struct LoadEditorTextureCommand : ICommand<services::EditorTextureHandle> {
        std::string path;
        bool isHDR = false;

        std::string_view getName() const override { return "LoadEditorTexture"; }
    };

    struct ReleaseEditorTextureCommand : ICommand<> {
        void* handle;

        std::string_view getName() const override { return "ReleaseEditorTexture"; }
    };

    // ============================================
    // QUERIES - Read-only operations
    // ============================================

    struct GetViewportTextureQuery : IQuery<services::ViewportTextureHandle> {
        std::string_view getName() const override { return "GetViewportTexture"; }
    };

    struct HasIBLQuery : IQuery<bool> {
        std::string_view getName() const override { return "HasIBL"; }
    };

    struct GetIBLPathQuery : IQuery<std::optional<std::string>> {
        std::string_view getName() const override { return "GetIBLPath"; }
    };

    // ============================================
    // NOTIFICATIONS - State change broadcasts
    // ============================================

    struct ViewportResizedNotification : INotification {
        uint32_t width;
        uint32_t height;

        std::string_view getName() const override { return "ViewportResized"; }
    };

    struct IBLChangedNotification : INotification {
        std::optional<std::string> hdrPath;  // nullopt if IBL removed

        std::string_view getName() const override { return "IBLChanged"; }
    };

    struct RenderFrameCompleteNotification : INotification {
        uint64_t frameNumber;
        double frameTimeMs;

        std::string_view getName() const override { return "RenderFrameComplete"; }
    };

    // ============================================
    // MESH COMMANDS - Mesh loading and rendering
    // ============================================
    
    struct LoadMeshCommand : ICommand<std::string> {
        std::string meshPath;

        std::string_view getName() const override { return "LoadMesh"; }
    };
    
    struct UnloadMeshCommand : ICommand<> {
        std::string meshId;

        std::string_view getName() const override { return "UnloadMesh"; }
    };
    
    struct UpdateMeshCameraCommand : ICommand<> {
        glm::mat4 viewMatrix;
        glm::mat4 projectionMatrix;
        glm::vec3 cameraPosition;
        float time = 0.0f;  // Animation time in seconds

        std::string_view getName() const override { return "UpdateMeshCamera"; }
    };

    // ============================================
    // MESH QUERIES
    // ============================================

    struct IsMeshLoadedQuery : IQuery<bool> {
        std::string meshPath;

        std::string_view getName() const override { return "IsMeshLoaded"; }
    };

    struct GetLoadedMeshesQuery : IQuery<std::vector<std::string>> {
        std::string_view getName() const override { return "GetLoadedMeshes"; }
    };

    struct GetMeshBoundingBoxQuery : IQuery<std::optional<services::MeshBoundingBox>> {
        std::string meshPath;

        std::string_view getName() const override { return "GetMeshBoundingBox"; }
    };

    // ============================================
    // BILLBOARD COMMANDS - Billboard icon visibility
    // ============================================

    struct SetShowBillboardIconsCommand : ICommand<> {
        bool show;

        std::string_view getName() const override { return "SetShowBillboardIcons"; }
    };

    struct LoadBillboardAtlasCommand : ICommand<bool> {
        std::string atlasPath;

        std::string_view getName() const override { return "LoadBillboardAtlas"; }
    };

    // ============================================
    // BILLBOARD QUERIES
    // ============================================

    struct GetShowBillboardIconsQuery : IQuery<bool> {
        std::string_view getName() const override { return "GetShowBillboardIcons"; }
    };

    // ============================================
    // DEBUG RENDERING COMMANDS
    // ============================================

    struct SetShowDebugRenderingCommand : ICommand<> {
        bool show;

        std::string_view getName() const override { return "SetShowDebugRendering"; }
    };

    // ============================================
    // DEBUG RENDERING QUERIES
    // ============================================

    struct GetShowDebugRenderingQuery : IQuery<bool> {
        std::string_view getName() const override { return "GetShowDebugRendering"; }
    };

    // ============================================
    // DEBUG/STATS QUERIES
    // ============================================

    struct GetCullingStatsQuery : IQuery<services::CullingDebugStats> {
        std::string_view getName() const override { return "GetCullingStats"; }
    };

    // ============================================
    // GRID COMMANDS
    // ============================================

    struct SetShowGridCommand : ICommand<> {
        bool show;

        std::string_view getName() const override { return "SetShowGrid"; }
    };

    // ============================================
    // GRID QUERIES
    // ============================================

    struct GetShowGridQuery : IQuery<bool> {
        std::string_view getName() const override { return "GetShowGrid"; }
    };

}
