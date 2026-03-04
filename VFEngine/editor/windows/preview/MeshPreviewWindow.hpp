#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/DTOs.hpp"
#include "data/AsyncLoadingTypes.hpp"
#include "animator/SocketTypes.hpp"
#include "resource/Types.hpp"
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

        math::AABB meshBounds;
        std::vector<services::SubMeshInfo> subMeshes;
        std::vector<services::LODInfo> lodLevels;
        int selectedSubMesh = -1;
        int selectedLOD = -1;

        bool isDraggingPreview = false;

        bool isOpen = true;
        bool needsInit = true;
        bool previewCleanedUp = false;

        services::MeshLoadingProgress loadingProgress;

        bool hasSkeleton = false;
        std::vector<std::string> boneNames;
        std::vector<animator::SocketDefinition> sockets;

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
        void drawSocketPanel();
        void drawLoadingIndicator(float width, float height);
        void onLoadingComplete();
        void loadSkeletonData();
        void handlePreviewInput();
    };
}
