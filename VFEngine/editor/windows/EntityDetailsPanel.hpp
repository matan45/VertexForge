#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/EntityHandle.hpp"
#include "events/EventDispatcher.hpp"
#include "details/TransformDrawer.hpp"
#include "details/CameraDrawer.hpp"
#include "details/IBLDrawer.hpp"
#include "details/MeshDrawer.hpp"
#include "details/MaterialDrawer.hpp"
#include "details/AudioSource2DDrawer.hpp"
#include "details/AudioSource3DDrawer.hpp"
#include "details/ScriptDrawer.hpp"
#include "details/ColliderDrawer.hpp"
#include "details/RigidBodyDrawer.hpp"
#include "details/VFXDrawer.hpp"
#include "details/AddComponentPopup.hpp"

namespace windows
{
    class EntityDetailsPanel : public controllers::imguiHandler::ImguiWindow
    {
    private:
        events::SubscriptionToken sceneClearedToken;

        // Component drawers
        details::TransformDrawer transformDrawer;
        details::CameraDrawer cameraDrawer;
        details::IBLDrawer iblDrawer;
        details::MeshDrawer meshDrawer;
        details::MaterialDrawer materialDrawer;
        details::AudioSource2DDrawer audio2DDrawer;
        details::AudioSource3DDrawer audio3DDrawer;
        details::ScriptDrawer scriptDrawer;
        details::ColliderDrawer colliderDrawer;
        details::RigidBodyDrawer rigidBodyDrawer;
        details::VFXDrawer vfxDrawer;
        details::AddComponentPopup addComponentPopup;

    public:
        explicit EntityDetailsPanel();
        ~EntityDetailsPanel() override;

        void draw() override;

    private:
        void drawDetails(services::EntityHandle handle);
        void drawEntityName(services::EntityHandle handle, const std::string& currentName);
        void drawEntityActiveCheckbox(services::EntityHandle handle, bool isActive);

        void subscribeToEvents();
        void onSceneCleared();

    public:
        // Shared UI styling helpers (used by drawers)
        static void pushComponentHeaderStyle();
        static void popComponentHeaderStyle();
        static void pushRemoveButtonStyle();
        static void popRemoveButtonStyle();
    };
}
