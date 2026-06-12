#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "ViewPortPicker.hpp"
#include "ViewPortGizmo.hpp"
#include "ViewPortOverlay.hpp"
#include "../../camera/EditorCamera.hpp"
#include <memory>

namespace windows
{
    struct CameraState
    {
        glm::mat4 viewMatrix;
        glm::mat4 projectionMatrix;
        glm::vec3 position;
        glm::vec3 forward;
    };

    class ViewPort : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::unique_ptr<editor::EditorCamera> editorCamera;

        ViewPortPicker picker;
        ViewPortGizmo gizmo;
        ViewPortOverlay overlay;

        bool isFirstMouseInput = true;
        float lastMouseX = 0.0f;
        float lastMouseY = 0.0f;

        bool sculptDragging = false;
        bool paintDragging = false;
        bool caveDragging = false;
        bool vegetationDragging = false;
        bool meshBrushDragging = false;

    public:
        explicit ViewPort();
        ~ViewPort() override = default;

        void draw() override;

        editor::EditorCamera* getEditorCamera() const { return editorCamera.get(); }

    private:
        void handleCameraInput();
        bool tryGetGameCameraState(CameraState& state, float aspectRatio);
        CameraState getActiveCameraState(bool isPlayMode, float aspectRatio);
        void updateRendererCameras(const CameraState& camera);
        void handleAssetDrop(glm::vec2 viewportPos, glm::vec2 viewportSize);
        glm::vec3 computeDropPosition(glm::vec2 mousePos, glm::vec2 viewportPos, glm::vec2 viewportSize);
        void spawnPrefabAt(const std::string& path, const glm::vec3& dropPos);
        void handleEntityPicking(bool isPlayMode, glm::vec2 viewportPos, glm::vec2 viewportSize);
        void drawSelectedUIOutline(glm::vec2 viewportPos, glm::vec2 viewportSize);
        void updateBrushCursors(glm::vec2 viewportPos, glm::vec2 viewportSize);
        void sendCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize);
        void updateSculptCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize);
        void handleSculptBrush();
        void updatePaintCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize);
        void handlePaintBrush();
        void updateHoleCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize);
        void handleHoleBrush();
        void handleCaveBrush();
        void updateVegetationCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize);
        void handleVegetationBrush();
        void updateMeshBrushCursorUV(glm::vec2 viewportPos, glm::vec2 viewportSize);
        void handleMeshBrush();
        void handleSplineTool();
    };
}
