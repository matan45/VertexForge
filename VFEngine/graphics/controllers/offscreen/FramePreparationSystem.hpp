#pragma once
#include "../../render/material/MaterialPBRExtractor.hpp"
#include "../../render/mesh/MeshTypes.hpp"
#include "../../render/tools/LightGizmoDebugRenderer.hpp"
#include <string>
#include <unordered_map>
#include <vector>

namespace render
{
    class RenderPassHandler;
}

namespace controllers::offscreen
{
    class SceneBVHManager;
    class LightBVHManager;
    class CameraController;

    struct FrameContext
    {
        render::RenderPassHandler* renderHandler = nullptr;
        SceneBVHManager* bvhManager = nullptr;
        LightBVHManager* lightBvhManager = nullptr;
        CameraController* cameraController = nullptr;
        bool playModeActive = false;
        bool showDebugRendering = true;
        bool showBillboardIcons = true;
        bool showGrid = true;
        bool showPhysicsDebug = false;
        bool showClusterDebug = false;
        float deltaTime = 0.0f;
        uint32_t viewportWidth = 0;
        uint32_t viewportHeight = 0;
    };

    class FramePreparationSystem
    {
    private:
        std::unordered_map<std::string, render::mesh::ExtractedPBRValues> pbrCache;

        const render::mesh::ExtractedPBRValues* getCachedPBRValues(const std::string& materialPath);
        void populateMaterialInfo(render::mesh::SubMeshMaterialInfo& matInfo, const std::string& materialPath);

        void collectDirectionalLightGizmos(std::vector<render::mesh::LightGizmoRenderData>& drawList);
        void collectPointLightGizmos(std::vector<render::mesh::LightGizmoRenderData>& drawList);
        void collectSpotLightGizmos(std::vector<render::mesh::LightGizmoRenderData>& drawList);

    public:
        FramePreparationSystem() = default;

        void prepareMeshes(const FrameContext& ctx);
        void prepareBillboards(const FrameContext& ctx);
        void prepareText(const FrameContext& ctx);
        void prepareCameraFrustums(const FrameContext& ctx);
        void prepareAudioSpheres(const FrameContext& ctx);
        void prepareGrid(const FrameContext& ctx);
        void preparePhysicsColliders(const FrameContext& ctx);
        void prepareLightGizmos(const FrameContext& ctx);
        void prepareClusterDebug(const FrameContext& ctx);
        void prepareUICanvasOutlines(const FrameContext& ctx);
        void prepareUIImages(const FrameContext& ctx);

        void invalidateMaterialCache(const std::string& materialPath);
        
    };
}
