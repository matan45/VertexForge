#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/DTOs.hpp"
#include "data/AsyncLoadingTypes.hpp"
#include <math/Frustum.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace editor
{
    class OrbitCamera;
}

namespace windows
{
    class MeshPreviewWindow : public controllers::imguiHandler::ImguiWindow
    {
    private:
        std::string meshPath;
        std::string windowTitle;
        std::unique_ptr<editor::OrbitCamera> camera;

        // Mesh preview
        math::AABB meshBounds; // Cached bounds for camera fitting
        std::vector<services::SubMeshInfo> subMeshes;
        // VK-300: lodLevels and selectedLOD removed - discrete LOD no longer used
        int selectedSubMesh = -1; // -1 = all submeshes

        // Camera input state
        bool isDraggingPreview = false;

        // Window state
        bool isOpen = true;
        bool needsInit = true;
        bool previewCleanedUp = false;

        // Async loading state
        services::MeshLoadingProgress loadingProgress;

    public:
        explicit MeshPreviewWindow(const std::string& meshFilePath);
        ~MeshPreviewWindow() override;

        void draw() override;

        bool shouldClose() const override { return !isOpen; }
        const std::string& getMeshPath() const { return meshPath; }

    private:
        void initRenderer();
        void updateAsyncLoading();
        void drawViewport(float width, float height);
        void drawSubMeshPanel();
        void drawLoadingIndicator(float width, float height);
        void onLoadingComplete();
        void handlePreviewInput();
    };
}
