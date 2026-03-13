#pragma once
#include "imguiHandler/ImguiWindow.hpp"
#include "data/EntityHandle.hpp"
#include "events/EventDispatcher.hpp"
#include "../details/TransformDrawer.hpp"
#include "../details/CameraDrawer.hpp"
#include "../details/IBLDrawer.hpp"
#include "../details/MeshDrawer.hpp"
#include "../details/MaterialDrawer.hpp"
#include "../details/AudioSource2DDrawer.hpp"
#include "../details/AudioSource3DDrawer.hpp"
#include "../details/ScriptDrawer.hpp"
#include "../details/ColliderDrawer.hpp"
#include "../details/RigidBodyDrawer.hpp"
#include "../details/PhysicsAnimationDrawer.hpp"
#include "../details/VFXDrawer.hpp"
#include "../details/BillboardDrawer.hpp"
#include "../details/TextDrawer.hpp"
#include "../details/DirectionalLightDrawer.hpp"
#include "../details/PointLightDrawer.hpp"
#include "../details/SpotLightDrawer.hpp"
#include "../details/TerrainDrawer.hpp"
#include "../details/TerrainTileDrawer.hpp"
#include "../details/WaterDrawer.hpp"
#include "../details/WaterTileDrawer.hpp"
#include "../details/UICanvasDrawer.hpp"
#include "../details/UIRectDrawer.hpp"
#include "../details/UIImageDrawer.hpp"
#include "../details/UILabelDrawer.hpp"
#include "../details/UIScrollDrawer.hpp"
#include "../details/UILayoutGroupDrawer.hpp"
#include "../details/UIButtonDrawer.hpp"
#include "../details/UITextInputDrawer.hpp"
#include "../details/UICheckboxDrawer.hpp"
#include "../details/UIDropdownDrawer.hpp"
#include "../details/UITabsDrawer.hpp"
#include "../details/UISliderDrawer.hpp"
#include "../details/UIProgressBarDrawer.hpp"
#include "../details/SocketAttachmentDrawer.hpp"
#include "../details/NavmeshAgentDrawer.hpp"
#include "../details/ControllerDrawer.hpp"
#include "../details/RenderTextureDrawer.hpp"
#include "../details/IKDrawer.hpp"
#include "../details/BehaviorTreeDrawer.hpp"
#include "../details/NavmeshRootDrawer.hpp"
#include "../details/WorldSectorDrawer.hpp"
#include "../details/AddComponentPopup.hpp"

namespace windows
{
    class EntityDetailsPanel : public controllers::imguiHandler::ImguiWindow
    {
    private:
        events::SubscriptionToken sceneClearedToken;

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
        details::PhysicsAnimationDrawer physicsAnimationDrawer;
        details::VFXDrawer vfxDrawer;
        details::BillboardDrawer billboardDrawer;
        details::TextDrawer textDrawer;
        details::DirectionalLightDrawer directionalLightDrawer;
        details::PointLightDrawer pointLightDrawer;
        details::SpotLightDrawer spotLightDrawer;
        details::TerrainDrawer terrainDrawer;
        details::TerrainTileDrawer terrainTileDrawer;
        details::WaterDrawer waterDrawer;
        details::WaterTileDrawer waterTileDrawer;
        details::UICanvasDrawer uiCanvasDrawer;
        details::UIRectDrawer uiRectDrawer;
        details::UIImageDrawer uiImageDrawer;
        details::UILabelDrawer uiLabelDrawer;
        details::UIScrollDrawer uiScrollDrawer;
        details::UILayoutGroupDrawer uiLayoutGroupDrawer;
        details::UIButtonDrawer uiButtonDrawer;
        details::UITextInputDrawer uiTextInputDrawer;
        details::UICheckboxDrawer uiCheckboxDrawer;
        details::UIDropdownDrawer uiDropdownDrawer;
        details::UITabsDrawer uiTabsDrawer;
        details::UISliderDrawer uiSliderDrawer;
        details::UIProgressBarDrawer uiProgressBarDrawer;
        details::SocketAttachmentDrawer socketAttachmentDrawer;
        details::NavmeshAgentDrawer navmeshAgentDrawer;
        details::ControllerDrawer controllerDrawer;
        details::RenderTextureDrawer renderTextureDrawer;
        details::IKDrawer ikDrawer;
        details::BehaviorTreeDrawer behaviorTreeDrawer;
        details::NavmeshRootDrawer navmeshRootDrawer;
        details::WorldSectorDrawer worldSectorDrawer;
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
