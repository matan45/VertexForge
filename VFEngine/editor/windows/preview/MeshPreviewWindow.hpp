#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "PreviewEnvironment.hpp"
#include "PreviewWindowChrome.hpp"
#include "data/DTOs.hpp"
#include "data/AsyncLoadingTypes.hpp"
#include "animator/SocketTypes.hpp"
#include "resource/Types.hpp"
#include <math/Frustum.hpp>
#include <glm/glm.hpp>
#include "ImGuizmo.h"
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
        editor::preview::PreviewEnvironment environment;

        math::AABB meshBounds;
        std::vector<services::SubMeshInfo> subMeshes;
        std::vector<services::LODInfo> lodLevels;
        int selectedSubMesh = -1;
        int selectedLOD = -1;

        bool isDraggingOrbit = false;
        bool isDraggingPan = false;

        // VK-1131 display options
        bool wireframeMode = false;
        bool showBoundingBox = false;
        int materialOverrideMode = 0;

        bool isOpen = true;
        bool needsInit = true;
        bool previewCleanedUp = false;

        editor::preview::WindowMaximizer maximizer;
        ImVec2 initialSize{0.0f, 0.0f};
        bool sizeSaved = false;

        services::MeshLoadingProgress loadingProgress;

        bool hasSkeleton = false;
        std::vector<std::string> boneNames;
        std::vector<animator::SocketDefinition> sockets;

        // VK-1427 Phase 3: static-mesh socket authoring (numeric fields + ImGuizmo).
        // Editing is enabled only for static meshes here; skeletal sockets stay
        // read-only (authored in the Animation Preview).
        int selectedSocketIndex = -1;
        char newSocketName[128] = "";
        glm::vec3 newSocketEulerDeg{0.0f}; // working-copy rotation (euler degrees) for socket creation
        bool socketSaveSuccess = false;
        float socketSaveMessageTimer = 0.0f;
        ImGuizmo::OPERATION socketGizmoOp = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE socketGizmoMode = ImGuizmo::LOCAL;

        // Preview-only mesh orientation (turntable buttons in the panel). Applied as
        // the render model matrix and composed into the socket gizmo; socket offsets
        // remain stored in mesh-local space regardless of this rotation.
        glm::quat meshPreviewRotation{1.0f, 0.0f, 0.0f, 0.0f};

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
        void drawStaticSocketEditor();
        void drawSocketSaveButton();
        void drawSocketGizmo();
        void drawLoadingIndicator(float width, float height);
        void onLoadingComplete();
        void loadSkeletonData();
        void sendEnvironmentParams();
    };
}
