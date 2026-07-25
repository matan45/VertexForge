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
#include "../details/VehicleDrawer.hpp"
#include "../details/BuoyancyDrawer.hpp"
#include "../details/WaterWakeEmitterDrawer.hpp"
#include "../details/DestructibleDrawer.hpp"
#include "../details/PhysicsAnimationDrawer.hpp"
#include "../details/VFXDrawer.hpp"
#include "../details/VFXSequenceDrawer.hpp"
#include "../details/BillboardDrawer.hpp"
#include "../details/TextDrawer.hpp"
#include "../details/DirectionalLightDrawer.hpp"
#include "../details/PointLightDrawer.hpp"
#include "../details/SpotLightDrawer.hpp"
#include "../details/TerrainDrawer.hpp"
#include "../details/TerrainTileDrawer.hpp"
#include "../details/OceanDrawer.hpp"
#include "../details/WaterBodyDrawer.hpp"
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
#include "../details/UIAnimationDrawer.hpp"
#include "../details/UIMaskDrawer.hpp"
#include "../details/UIStyleDrawer.hpp"
#include "../details/UITooltipDrawer.hpp"
#include "../details/UIWindowDrawer.hpp"
#include "../details/UIListViewDrawer.hpp"
#include "../details/UIDraggableDrawer.hpp"
#include "../details/UIDropTargetDrawer.hpp"
#include "../details/SocketAttachmentDrawer.hpp"
#include "../details/NavmeshAgentDrawer.hpp"
#include "../details/OffMeshLinkDrawer.hpp"
#include "../details/NavmeshObstacleDrawer.hpp"
#include "../details/NavmeshModifierVolumeDrawer.hpp"
#include "../details/NavInvokerDrawer.hpp"
#include "../details/VolumetricVolumeDrawer.hpp"
#include "../details/VolumetricAgentDrawer.hpp"
#include "../details/ControllerDrawer.hpp"
#include "../details/RenderTextureDrawer.hpp"
#include "../details/IKDrawer.hpp"
#include "../details/BehaviorTreeDrawer.hpp"
#include "../details/DecalDrawer.hpp"
#include "../details/ReverbZoneDrawer.hpp"
#include "../details/FogVolumeDrawer.hpp"
#include "../details/ReflectionProbeDrawer.hpp"
#include "../details/NavmeshRootDrawer.hpp"
#include "../details/WorldSectorDrawer.hpp"
#include "../details/MetaComponentDrawer.hpp"
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
        details::VehicleDrawer vehicleDrawer;
        details::BuoyancyDrawer buoyancyDrawer;
        details::WaterWakeEmitterDrawer waterWakeEmitterDrawer;   // VK-1606
        details::DestructibleDrawer destructibleDrawer;
        details::PhysicsAnimationDrawer physicsAnimationDrawer;
        details::VFXDrawer vfxDrawer;
        details::VFXSequenceDrawer vfxSequenceDrawer;
        details::BillboardDrawer billboardDrawer;
        details::TextDrawer textDrawer;
        details::DirectionalLightDrawer directionalLightDrawer;
        details::PointLightDrawer pointLightDrawer;
        details::SpotLightDrawer spotLightDrawer;
        details::TerrainDrawer terrainDrawer;
        details::TerrainTileDrawer terrainTileDrawer;
        details::OceanDrawer oceanDrawer;
        details::WaterBodyDrawer waterBodyDrawer;   // VK-1607
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
        details::UIAnimationDrawer uiAnimationDrawer;
        details::UIMaskDrawer uiMaskDrawer;
        details::UIStyleDrawer uiStyleDrawer;
        details::UITooltipDrawer uiTooltipDrawer;
        details::UIWindowDrawer uiWindowDrawer;
        details::UIListViewDrawer uiListViewDrawer;
        details::UIDraggableDrawer uiDraggableDrawer;
        details::UIDropTargetDrawer uiDropTargetDrawer;
        details::SocketAttachmentDrawer socketAttachmentDrawer;
        details::NavmeshAgentDrawer navmeshAgentDrawer;
        details::OffMeshLinkDrawer offMeshLinkDrawer;
        details::NavmeshObstacleDrawer navmeshObstacleDrawer;
        details::NavmeshModifierVolumeDrawer navmeshModifierVolumeDrawer;
        details::NavInvokerDrawer navInvokerDrawer;
        details::VolumetricVolumeDrawer volumetricVolumeDrawer;
        details::VolumetricAgentDrawer volumetricAgentDrawer;
        details::ControllerDrawer controllerDrawer;
        details::RenderTextureDrawer renderTextureDrawer;
        details::IKDrawer ikDrawer;
        details::BehaviorTreeDrawer behaviorTreeDrawer;
        details::DecalDrawer decalDrawer;
        details::ReverbZoneDrawer reverbZoneDrawer;
        details::FogVolumeDrawer fogVolumeDrawer;
        details::ReflectionProbeDrawer reflectionProbeDrawer;
        details::NavmeshRootDrawer navmeshRootDrawer;
        details::WorldSectorDrawer worldSectorDrawer;
        details::MetaComponentDrawer metaComponentDrawer;
        details::AddComponentPopup addComponentPopup;

    public:
        explicit EntityDetailsPanel();
        ~EntityDetailsPanel() override;

        void draw() override;

        // VK-1433 Phase 4 — reusable inline component inspector for an arbitrary entity (no
        // ImGui::Begin window, no name/active/prefab header). Draws the FULL per-component drawer
        // stack (edit + per-header Remove) followed by the multi-section Add Component popup, exactly
        // like drawDetails() does for the global selection. The Prefab Rig Preview window embeds this
        // for its selected sandbox entity so it gets the shared inspector without re-listing every
        // drawer. Caller owns the surrounding layout (child region, separators).
        void drawComponentSection(services::EntityHandle handle);

    private:
        void drawDetails(services::EntityHandle handle);
        void drawEntityName(services::EntityHandle handle, const std::string& currentName);
        void drawEntityActiveCheckbox(services::EntityHandle handle, bool isActive, bool isEffectivelyActive);
        void drawPrefabControls(services::EntityHandle handle);

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
