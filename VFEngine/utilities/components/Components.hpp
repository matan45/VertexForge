#pragma once
#include "CoreComponents.hpp"
#include "MediaComponents.hpp"
#include "PhysicsComponents.hpp"
#include "PhysicsAnimationComponent.hpp"
#include "LightTextComponents.hpp"
#include "TerrainComponents.hpp"
#include "WaterComponents.hpp"
#include "UIComponents.hpp"

namespace components
{
    using OptionalComponents = entt::type_list<IBLComponent, CameraComponent, MeshComponent, MaterialComponent,
                                               BillboardComponent, AudioSource2DComponent, AudioSource3DComponent,
                                               ScriptComponent, ColliderComponent, RigidBodyComponent, AnimatorComponent,
                                               PhysicsAnimationComponent,
                                               VFXComponent, DirectionalLightComponent, PointLightComponent,
                                               SpotLightComponent, TerrainComponent, TerrainTileComponent,
                                               WaterComponent, WaterTileComponent, TextComponent,
                                               UICanvasComponent, UIRectComponent, UIImageComponent, UIScrollComponent,
                                               UILayoutGroupComponent, UILabelComponent, UIButtonComponent,
                                               UITextInputComponent, UIDropdownComponent,
                                               UITabsComponent, UISliderComponent>;
}
