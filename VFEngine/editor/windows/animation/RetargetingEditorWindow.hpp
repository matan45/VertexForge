#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "AnimationViewport.hpp"
#include "../preview/PreviewEnvironment.hpp"
#include "../preview/PreviewWindowChrome.hpp"
#include "resource/Types.hpp"
#include "retargeting/RetargetTypes.hpp"
#include "providers/PreviewInstanceId.hpp"
#include "providers/animation/IAnimationPreviewProvider.hpp"
#include <memory>
#include <string>
#include <vector>

namespace editor { class OrbitCamera; }

namespace windows
{
    // Authoring window for VK-910 animation retargeting. Loads a source and a target
    // skinned mesh, auto-maps each skeleton to humanoid roles (editable), shows a 2D
    // skeleton overlay, and saves two .vfrig profiles + the .vfretarget binding that
    // ties them together. Opened from the Tools menu (new) or by double-clicking a
    // .vfretarget in the content browser (edit).
    class RetargetingEditorWindow : public controllers::imguiHandler::ImguiWindow
    {
    public:
        explicit RetargetingEditorWindow(const std::string& filePath);
        ~RetargetingEditorWindow() override;

        void draw() override;
        bool shouldClose() const override { return !isOpen; }

    private:
        struct Side
        {
            std::string meshPath;
            std::string meshName;
            resource::SkeletonData skeleton;
            retargeting::HumanoidRigData rig;
            std::string rigPath;     // resolved on save
            std::string rigGuid;     // hex GUID of the saved .vfrig
            bool loaded = false;
            int selectedRole = 0;    // for overlay highlight
        };

        void drawSide(const char* id, Side& side);
        void drawRoleTable(const char* id, Side& side);
        void drawSkeletonOverlay(const char* id, const Side& side);
        void drawPreview();
        bool loadMesh(Side& side, const std::string& meshPath);
        bool loadRigInto(Side& side, const std::string& rigPath); // edit-existing path
        void save();
        std::string saveRig(Side& side);
        void loadExisting();

        void initPreview();
        void cleanUpPreview();
        void updateBonesFromService();
        services::PreviewInstanceId previewInstanceId() const
        {
            return services::PreviewInstanceId(const_cast<RetargetingEditorWindow*>(this));
        }

        Side source;
        Side target;
        retargeting::RetargetMapData mapData;
        std::string retargetPath;     // .vfretarget being edited (empty until first save)
        std::string windowTitle;
        std::string statusMessage;
        bool isOpen = true;
        bool triedLoadExisting = false;

        // Live 3D retarget preview (reuses the AnimationPreview CQRS).
        std::unique_ptr<editor::OrbitCamera> camera;
        windows::animation::AnimationViewport viewport;
        editor::preview::PreviewEnvironment environment;
        std::vector<services::EvaluatedBoneInfo> evaluatedBones;
        std::string sourceAnimPath;
        std::string loadedPreviewMeshPath;
        bool previewInitialized = false;
        bool previewCleanedUp = false;
        bool meshInPreview = false;
        bool animInPreview = false;
        bool isDraggingPreview = false;
        bool isDraggingPan = false;
        int selectedChannel = -1;

        editor::preview::WindowMaximizer maximizer;
        bool sizeSaved = false;
    };
}
