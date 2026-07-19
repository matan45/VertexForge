#pragma once
#include "CoreComponents.hpp"
#include "MediaComponents.hpp"
#include "PhysicsComponents.hpp"
#include "PhysicsAnimationComponent.hpp"
#include "LightTextComponents.hpp"
#include "TerrainComponents.hpp"
#include "OceanComponents.hpp"
#include "UIComponents.hpp"
#include "NavmeshComponents.hpp"
#include "ControllerComponents.hpp"
#include "IKComponent.hpp"
#include "VegetationComponents.hpp"
#include "MeshBrushComponents.hpp"
#include "DecalComponents.hpp"
#include "VolumetricComponents.hpp"
#include "WeatherComponents.hpp"
#include "DestructionComponents.hpp"

namespace components
{
    // VK-1433 Phase 4 — marks the Prefab Rig Preview window's transient editing sandbox root.
    // Mirrors UIPreviewTagComponent: the sandbox subtree is editor-only — the scene serializer
    // skips it (so it never bakes into the user's saved scene) and the .vfPrefab serializer
    // normalizes a tagged root back to isActive=true (the window holds the root inactive so the
    // main passes ignore it, but a saved prefab must instantiate visible). Never serialized;
    // lives only while a Prefab Rig Preview window is open, and gives the window an exact
    // teardown handle on close.
    struct PreviewSandboxTagComponent
    {
    };

    using OptionalComponents = entt::type_list<IBLComponent, CameraComponent, MeshComponent, MaterialComponent,
                                               BillboardComponent, AudioSource2DComponent, AudioSource3DComponent,
                                               ScriptComponent, ColliderComponent, RigidBodyComponent, AnimatorComponent,
                                               VehicleComponent, PhysicsAnimationComponent,
                                               VFXComponent, VFXSequenceComponent, DirectionalLightComponent, PointLightComponent,
                                               SpotLightComponent, TerrainComponent, TerrainTileComponent,
                                               OceanComponent, TextComponent,
                                               UICanvasComponent, UIRectComponent, UIImageComponent, UIScrollComponent,
                                               UILayoutGroupComponent, UILabelComponent, UIButtonComponent,
                                               UITextInputComponent, UIDropdownComponent,
                                               UITabsComponent, UISliderComponent,
                                               UICheckboxComponent, UIProgressBarComponent, UIMaskComponent,
                                               UIDraggableComponent, UIDropTargetComponent, UIAnimationComponent,
                                               UIStyleComponent, UITooltipComponent, UIWindowComponent,
                                               UIListViewComponent,
                                               SocketAttachmentComponent, SocketOverrideComponent,
                                               NavmeshAgentComponent, NavmeshComponent, OffMeshLinkComponent, NavmeshObstacleComponent, NavmeshModifierVolumeComponent, NavInvokerComponent, ControllerComponent,
                                               IKTargetComponent, WorldSectorComponent,
                                               GrassComponent, MeshBrushInstanceComponent,
                                               BehaviorTreeComponent, DecalComponent, ReverbZoneComponent,
                                               FogVolumeComponent, ReflectionProbeComponent,
                                               VolumetricNavVolumeComponent, VolumetricAgentComponent,
                                               WeatherZoneComponent,
                                               DestructibleComponent, FragmentComponent>;
}
