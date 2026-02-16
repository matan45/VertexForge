#pragma once
#include "../../render/tools/LightGizmoDebugRenderer.hpp"
#include "../../render/mesh/MeshTypes.hpp"
#include <vector>

namespace render::mesh
{
    struct PhysicsColliderRenderData;
    struct UICanvasOutlineRenderData;
    struct ClusterDebugRenderData;
}

namespace controllers::offscreen
{
    struct FrameContext;

    class DebugFrameBuilder
    {
    public:
        DebugFrameBuilder() = default;

        void prepareCameraFrustums(const FrameContext& ctx);
        void prepareAudioSpheres(const FrameContext& ctx);
        void prepareGrid(const FrameContext& ctx);
        void preparePhysicsColliders(const FrameContext& ctx);
        void prepareLightGizmos(const FrameContext& ctx);
        void prepareClusterDebug(const FrameContext& ctx);
        void prepareUICanvasOutlines(const FrameContext& ctx);

    private:
        void collectDirectionalLightGizmos(std::vector<render::mesh::LightGizmoRenderData>& drawList);
        void collectPointLightGizmos(std::vector<render::mesh::LightGizmoRenderData>& drawList);
        void collectSpotLightGizmos(std::vector<render::mesh::LightGizmoRenderData>& drawList);
        void collectStandardColliders(std::vector<render::mesh::PhysicsColliderRenderData>& drawList);
        void collectTerrainColliders(std::vector<render::mesh::PhysicsColliderRenderData>& drawList);
        void collectUIRectOutlines(std::vector<render::mesh::UICanvasOutlineRenderData>& drawList);
        void collectLightClusterHighlights(const FrameContext& ctx,
                                           render::mesh::ClusterDebugRenderData& debugData);
    };
}
