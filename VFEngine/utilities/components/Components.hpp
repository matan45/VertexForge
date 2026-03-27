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
#include "PluginComponents.hpp"

namespace components
{
    using OptionalComponents = entt::type_list<IBLComponent, CameraComponent, MeshComponent, MaterialComponent,
                                               BillboardComponent, AudioSource2DComponent, AudioSource3DComponent,
                                               ScriptComponent, ColliderComponent, RigidBodyComponent, AnimatorComponent,
                                               PhysicsAnimationComponent,
                                               VFXComponent, DirectionalLightComponent, PointLightComponent,
                                               SpotLightComponent, TerrainComponent, TerrainTileComponent,
                                               OceanComponent, TextComponent,
                                               UICanvasComponent, UIRectComponent, UIImageComponent, UIScrollComponent,
                                               UILayoutGroupComponent, UILabelComponent, UIButtonComponent,
                                               UITextInputComponent, UIDropdownComponent,
                                               UITabsComponent, UISliderComponent,
                                               SocketAttachmentComponent, SocketOverrideComponent,
                                               NavmeshAgentComponent, NavmeshComponent, OffMeshLinkComponent, NavmeshObstacleComponent, NavmeshModifierVolumeComponent, NavInvokerComponent, ControllerComponent,
                                               IKTargetComponent, WorldSectorComponent,
                                               GrassComponent, MeshBrushInstanceComponent,
                                               BehaviorTreeComponent, DecalComponent, ReverbZoneComponent,
                                               FogVolumeComponent,
                                               VolumetricNavVolumeComponent, VolumetricAgentComponent>;
}
