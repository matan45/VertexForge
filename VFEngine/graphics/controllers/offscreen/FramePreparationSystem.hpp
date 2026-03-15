#pragma once
#include "DebugFrameBuilder.hpp"
#include "UIFrameBuilder.hpp"
#include "UIInteractionSystem.hpp"
#include "UIAnimationSystem.hpp"
#include "../../render/material/MaterialPBRExtractor.hpp"
#include "../../render/mesh/MeshTypes.hpp"
#include "../../render/billboard/BillboardTypes.hpp"
#include "../../render/text/TextTypes.hpp"
#include "../../../services/providers/render/IDecalRenderProvider.hpp"
#include <string>
#include <functional>
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
        glm::vec2 mousePosition{0.0f, 0.0f};
        glm::vec2 scrollDelta{0.0f, 0.0f};
        bool leftMouseDown = false;
        bool leftMousePressed = false;   // rising edge (was up, now down)
        bool leftMouseReleased = false;  // falling edge (was down, now up)
        bool leftMouseDoubleClick = false;

        // Keyboard/text input data (for UITextInput interaction)
        std::vector<uint32_t> charInput;
        // Key state query function (key code -> pressed this frame)
        // Uses GLFW key codes as integer values
        std::function<bool(int)> isKeyPressed;
        std::function<bool(int)> isKeyDown;
        std::function<std::string()> getClipboardText;
        std::function<void(const std::string&)> setClipboardText;
    };

    class FramePreparationSystem
    {
    private:
        std::unordered_map<std::string, render::mesh::ExtractedPBRValues> pbrCache;
        DebugFrameBuilder debugBuilder;
        UIFrameBuilder uiFrameBuilder;
        UIInteractionSystem uiInteraction;
        UIAnimationSystem uiAnimation;

        const render::mesh::ExtractedPBRValues* getCachedPBRValues(const std::string& materialPath);
        void populateMaterialInfo(render::mesh::SubMeshMaterialInfo& matInfo, const std::string& materialPath);

    public:
        FramePreparationSystem() = default;

        void prepareMeshes(const FrameContext& ctx);
        void prepareBillboards(const FrameContext& ctx);
        void prepareText(const FrameContext& ctx);

        // Parallel variant: runs billboard and text data gathering concurrently
        // with mesh preparation. Vulkan pipeline init stays on main thread.
        void prepareSceneData(const FrameContext& ctx);

    private:
        std::vector<render::billboard::BillboardRenderData> gatherBillboardData(const FrameContext& ctx);
        std::vector<render::text::TextRenderData> gatherTextData(const FrameContext& ctx);
        void prepareDecals(const FrameContext& ctx);

    public:

        void prepareCameraFrustums(const FrameContext& ctx) { debugBuilder.prepareCameraFrustums(ctx); }
        void prepareAudioSpheres(const FrameContext& ctx) { debugBuilder.prepareAudioSpheres(ctx); }
        void prepareReverbZones(const FrameContext& ctx) { debugBuilder.prepareReverbZones(ctx); }
        void prepareGrid(const FrameContext& ctx) { debugBuilder.prepareGrid(ctx); }
        void preparePhysicsColliders(const FrameContext& ctx) { debugBuilder.preparePhysicsColliders(ctx); }
        void prepareLightGizmos(const FrameContext& ctx) { debugBuilder.prepareLightGizmos(ctx); }
        void prepareClusterDebug(const FrameContext& ctx) { debugBuilder.prepareClusterDebug(ctx); }
        void prepareUICanvasOutlines(const FrameContext& ctx) { debugBuilder.prepareUICanvasOutlines(ctx); }

        void prepareUIImages(const FrameContext& ctx) { uiFrameBuilder.prepareUIImages(ctx, uiInteraction, uiAnimation); }
        void prepareUILabels(const FrameContext& ctx) { uiFrameBuilder.prepareUILabels(ctx); }

        void invalidateMaterialCache(const std::string& materialPath);
    };
}
