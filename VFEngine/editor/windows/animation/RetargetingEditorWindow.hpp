#pragma once

#include "imguiHandler/ImguiWindow.hpp"
#include "resource/Types.hpp"
#include "retargeting/RetargetTypes.hpp"
#include <string>

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
        ~RetargetingEditorWindow() override = default;

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
        bool loadMesh(Side& side, const std::string& meshPath);
        bool loadRigInto(Side& side, const std::string& rigPath); // edit-existing path
        void save();
        std::string saveRig(Side& side);
        void loadExisting();

        Side source;
        Side target;
        retargeting::RetargetMapData mapData;
        std::string retargetPath;     // .vfretarget being edited (empty until first save)
        std::string windowTitle;
        std::string statusMessage;
        bool isOpen = true;
        bool triedLoadExisting = false;
    };
}
