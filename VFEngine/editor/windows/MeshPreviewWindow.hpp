#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/DTOs.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace editor
{
    class OrbitCamera;
}

namespace controllers
{
    class MeshPreviewController;
}

namespace windows
{
    class MeshPreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string meshPath;
        std::string windowTitle;
        std::unique_ptr<editor::OrbitCamera> camera;
        std::unique_ptr<controllers::MeshPreviewController> controller;

        // Mesh data
        std::vector<services::SubMeshInfo> subMeshes;
        int selectedSubMesh = -1;  // -1 = all submeshes

        // Mesh transform
        float meshScale = 1.0f;
        glm::vec3 meshRotation{ 0.0f };  // Euler angles in degrees

        // Window state
        bool isOpen = true;
        bool needsInit = true;

        // Mouse tracking for camera
        bool isDragging = false;
        float lastMouseX = 0.0f;
        float lastMouseY = 0.0f;

    public:
        explicit MeshPreviewWindow(const std::string& meshFilePath);
        ~MeshPreviewWindow() override;

        void draw() override;

        bool shouldClose() const override { return !isOpen; }
        const std::string& getMeshPath() const { return meshPath; }

    private:
        void initRenderer();
        void drawViewport(float width, float height);
        void drawSubMeshPanel();
        void handleCameraInput(bool imageHovered);
    };
}
