#pragma once
#include "EventTypes.hpp"
#include "../data/DTOs.hpp"
#include "../data/AsyncLoadingTypes.hpp"
#include "../providers/IOffScreenProvider.hpp"
#include "resource/Types.hpp"
#include "types/RenderSettings.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

namespace events::render {

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

    struct LoadEditorTextureFromDataCommand : ICommand<services::EditorTextureHandle> {
        mutable resource::TextureData textureData;  // mutable to allow move from const ref

        std::string_view getName() const override { return "LoadEditorTextureFromData"; }
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

    struct SetShowPhysicsDebugCommand : ICommand<> {
        bool show;

        std::string_view getName() const override { return "SetShowPhysicsDebug"; }
    };

    struct SetViewModeCommand : ICommand<> {
        uint32_t mode;  // 0=Color, 1=Meshlet, 2=LOD, 3=Mipmap, 4=Cluster

        std::string_view getName() const override { return "SetViewMode"; }
    };

    struct LoadBillboardAtlasCommand : ICommand<bool> {
        std::string atlasPath;

        std::string_view getName() const override { return "LoadBillboardAtlas"; }
    };

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

    struct GetShowPhysicsDebugQuery : IQuery<bool> {
        std::string_view getName() const override { return "GetShowPhysicsDebug"; }
    };

    struct GetViewModeQuery : IQuery<uint32_t> {
        std::string_view getName() const override { return "GetViewMode"; }
    };

    struct SetShowShadowDebugCommand : ICommand<> {
        bool show;

        std::string_view getName() const override { return "SetShowShadowDebug"; }
    };

    struct GetShowShadowDebugQuery : IQuery<bool> {
        std::string_view getName() const override { return "GetShowShadowDebug"; }
    };

    struct GetCullingStatsQuery : IQuery<services::CullingDebugStats> {
        std::string_view getName() const override { return "GetCullingStats"; }
    };

    struct ApplyShadowSettingsCommand : ICommand<> {
        types::RenderSettings settings;

        std::string_view getName() const override { return "ApplyShadowSettings"; }
    };

    struct GetShadowStatsQuery : IQuery<services::ShadowStats> {
        std::string_view getName() const override { return "GetShadowStats"; }
    };

    struct SetFrustumCullingCommand : ICommand<> {
        bool enabled;

        std::string_view getName() const override { return "SetFrustumCulling"; }
    };

    struct SetOcclusionCullingCommand : ICommand<> {
        bool enabled;

        std::string_view getName() const override { return "SetOcclusionCulling"; }
    };

    struct SetLODSelectionCommand : ICommand<> {
        bool enabled;

        std::string_view getName() const override { return "SetLODSelection"; }
    };

    struct SetMeshletFrustumCullingCommand : ICommand<> {
        bool enabled;

        std::string_view getName() const override { return "SetMeshletFrustumCulling"; }
    };

    struct SetMeshletBackfaceCullingCommand : ICommand<> {
        bool enabled;

        std::string_view getName() const override { return "SetMeshletBackfaceCulling"; }
    };

    struct SetTerrainFrustumCullingCommand : ICommand<> {
        bool enabled;

        std::string_view getName() const override { return "SetTerrainFrustumCulling"; }
    };

    struct SetTerrainMeshletCullingCommand : ICommand<> {
        bool enabled;

        std::string_view getName() const override { return "SetTerrainMeshletCulling"; }
    };

    struct SetTerrainRenderingEnabledCommand : ICommand<> {
        bool enabled;

        std::string_view getName() const override { return "SetTerrainRenderingEnabled"; }
    };

    struct SetTerrainLODBiasCommand : ICommand<> {
        float bias;

        std::string_view getName() const override { return "SetTerrainLODBias"; }
    };

    struct SetTerrainErrorThresholdCommand : ICommand<> {
        float threshold;

        std::string_view getName() const override { return "SetTerrainErrorThreshold"; }
    };

    struct SetTerrainTextureScaleCommand : ICommand<> {
        float scale;

        std::string_view getName() const override { return "SetTerrainTextureScale"; }
    };

    struct SetTerrainShadowLODCommand : ICommand<> {
        uint32_t lod;

        std::string_view getName() const override { return "SetTerrainShadowLOD"; }
    };

    struct SetShowNavmeshDebugCommand : ICommand<> {
        bool show;

        std::string_view getName() const override { return "SetShowNavmeshDebug"; }
    };

    struct GetShowNavmeshDebugQuery : IQuery<bool> {
        std::string_view getName() const override { return "GetShowNavmeshDebug"; }
    };

    struct UpdateNavmeshDebugMeshCommand : ICommand<> {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;

        std::string_view getName() const override { return "UpdateNavmeshDebugMesh"; }
    };

    struct ClearNavmeshDebugMeshCommand : ICommand<> {
        std::string_view getName() const override { return "ClearNavmeshDebugMesh"; }
    };

    struct SetUIViewportOffsetCommand : ICommand<> {
        glm::vec2 offset{0.0f, 0.0f};
        glm::vec2 panelSize{0.0f, 0.0f};

        std::string_view getName() const override { return "SetUIViewportOffset"; }
    };

}
