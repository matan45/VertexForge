#pragma once
#include "../../render/material/MaterialPBRExtractor.hpp"
#include "../../render/mesh/MeshTypes.hpp"
#include "../../render/tools/LightGizmoDebugRenderer.hpp"
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
        void prepareUILabels(const FrameContext& ctx);

        void invalidateMaterialCache(const std::string& materialPath);

    private:
        void prepareUIImagesScreenSpace(const FrameContext& ctx);
        void prepareUIImagesWorldSpace(const FrameContext& ctx);
        void prepareUILabelsScreenSpace(const FrameContext& ctx);
        void prepareUILabelsWorldSpace(const FrameContext& ctx);
        void processUIButtonInteraction(const FrameContext& ctx);
        void processUICheckboxInteraction(const FrameContext& ctx);
        void processUITextInputInteraction(const FrameContext& ctx);
        void processUIDropdownInteraction(const FrameContext& ctx);

        // Track which text input entity is currently focused (-1 = none)
        entt::entity focusedTextInput = entt::null;
    };
}
