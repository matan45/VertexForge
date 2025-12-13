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
        
        std::vector<services::SubMeshInfo> subMeshes;
        int selectedSubMesh = -1;  // -1 = all submeshes
        
        glm::vec3 meshPosition{ 0.0f };
        glm::vec3 meshRotation{ 0.0f };  // Euler angles in degrees
        float meshScale = 1.0f;

        // Window state
        bool isOpen = true;
        bool needsInit = true;

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
    };
}
