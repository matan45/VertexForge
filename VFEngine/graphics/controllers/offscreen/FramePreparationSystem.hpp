#pragma once
#include "../../render/material/MaterialPBRExtractor.hpp"
#include "math/Frustum.hpp"
#include <string>
#include <unordered_map>

namespace render
{
    class RenderPassHandler;
}

namespace controllers::offscreen
{
    class SceneBVHManager;
    class CameraController;

    struct FrameContext
    {
        render::RenderPassHandler* renderHandler = nullptr;
        SceneBVHManager* bvhManager = nullptr;
        CameraController* cameraController = nullptr;
        bool playModeActive = false;
        bool showDebugRendering = true;
        bool showBillboardIcons = true;
        bool showGrid = true;
    };

    class FramePreparationSystem
    {
    public:
        FramePreparationSystem() = default;

        void prepareMeshes(const FrameContext& ctx);
        void prepareBillboards(const FrameContext& ctx);
        void prepareCameraFrustums(const FrameContext& ctx);
        void prepareAudioSpheres(const FrameContext& ctx);
        void prepareGrid(const FrameContext& ctx);

        void invalidateMaterialCache(const std::string& materialPath);

    private:
        std::unordered_map<std::string, render::mesh::ExtractedPBRValues> pbrCache;
    };
}
