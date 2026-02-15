#pragma once
#include "../../render/tools/LightGizmoDebugRenderer.hpp"
#include "../../render/mesh/MeshTypes.hpp"
#include <vector>

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
    };
}
