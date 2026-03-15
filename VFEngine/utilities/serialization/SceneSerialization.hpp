#pragma once
#include <string_view>
#include <functional>
#include <nlohmann/json.hpp>
#include "../scene/Entity.hpp"
#include "../types/PhysicsTypes.hpp"
#include "../types/PhysicsAnimationTypes.hpp"
#include "../types/NavmeshTypes.hpp"
#include "../types/AudioTypes.hpp"
#include "../types/RenderSettings.hpp"

namespace scene
{
    class SceneGraphSystem;
}

namespace serialization
{
    using json = nlohmann::json;

    using SceneLoadProgressCallback = std::function<void(const std::string&, size_t, size_t)>;

    class SceneSerialization
    {
        friend class PrefabSerialization;

    public:
        // Extension point for plugin component serialization.
        // Set by plugin module to bridge Utilities -> Plugin without direct dependency.
        using PluginSerializeFn = std::function<nlohmann::json(scene::Entity&)>;
        using PluginDeserializeFn = std::function<void(const nlohmann::json&, scene::Entity&)>;
        static void setPluginSerializationHooks(PluginSerializeFn serialize, PluginDeserializeFn deserialize);

        static scene::SceneGraphSystem loadScene(std::string_view filename);
        static bool loadSceneInto(std::string_view filename, scene::SceneGraphSystem& sceneGraph,
                                  SceneLoadProgressCallback progressCallback = nullptr);
        static bool saveScene(scene::SceneGraphSystem& sceneGraph, std::string_view filename);

        static json createSnapshot(scene::SceneGraphSystem& sceneGraph);
        static bool restoreFromSnapshot(const json& snapshot, scene::SceneGraphSystem& sceneGraph,
                                        SceneLoadProgressCallback progressCallback = nullptr);

        static json serializeEntity(scene::Entity& entity);
        static void deserializeEntity(const json& entityJson, scene::Entity& entity,
                                      scene::SceneGraphSystem& sceneGraph, bool isRoot,
                                      SceneLoadProgressCallback progressCallback, size_t& entitiesLoaded,
                                      size_t totalEntities);

    private:
        static json serializeRootEntity(scene::Entity& root);
        static json serializeEntityComponents(scene::Entity& entity);
        static void deserializeEntityComponents(const json& componentsJson, scene::Entity& entity);
        static void deserializeSceneSettings(const json& sceneJson, scene::SceneGraphSystem& sceneGraph);

        static size_t countEntities(const json& entityJson);

        static void deserializeChildren(const json& childrenJson, scene::Entity& parent,
                                        scene::SceneGraphSystem& sceneGraph,
                                        SceneLoadProgressCallback progressCallback, size_t& entitiesLoaded,
                                        size_t totalEntities);

        static json serializeTransform(const components::TransformComponent& transform);
        static void deserializeTransform(const json& j, components::TransformComponent& transform);

        static json serializeCamera(const components::CameraComponent& camera);
        static void deserializeCamera(const json& j, components::CameraComponent& camera);

        static json serializeIBL(const components::IBLComponent& ibl);
        static std::string deserializeIBL(const json& j);

        static json serializeMesh(const components::MeshComponent& mesh);
        static void deserializeMesh(const json& j, components::MeshComponent& mesh);

        static json serializeMaterial(const components::MaterialComponent& material);
        static void deserializeMaterial(const json& j, components::MaterialComponent& material);

        static json serializeBillboard(const components::BillboardComponent& billboard);
        static void deserializeBillboard(const json& j, components::BillboardComponent& billboard);

        static json serializeText(const components::TextComponent& text);
        static void deserializeText(const json& j, components::TextComponent& text);

        static json serializeAudioSource2D(const components::AudioSource2DComponent& audioSource);
        static void deserializeAudioSource2D(const json& j, components::AudioSource2DComponent& audioSource);

        static json serializeAudioSource3D(const components::AudioSource3DComponent& audioSource);
        static void deserializeAudioSource3D(const json& j, components::AudioSource3DComponent& audioSource);

        static json serializeReverbZone(const components::ReverbZoneComponent& zone);
        static void deserializeReverbZone(const json& j, components::ReverbZoneComponent& zone);

        static json serializeScript(const components::ScriptComponent& script);
        static void deserializeScript(const json& j, components::ScriptComponent& script);

        static json serializeCollider(const components::ColliderComponent& collider);
        static void deserializeCollider(const json& j, components::ColliderComponent& collider);

        static json serializeRigidBody(const components::RigidBodyComponent& rigidBody);
        static void deserializeRigidBody(const json& j, components::RigidBodyComponent& rigidBody);

        static json serializeVFX(const components::VFXComponent& vfx);
        static void deserializeVFX(const json& j, components::VFXComponent& vfx);

        static json serializePhysicsAnimation(const components::PhysicsAnimationComponent& physAnim);
        static void deserializePhysicsAnimation(const json& j, components::PhysicsAnimationComponent& physAnim);

        static std::string physicsAnimationModeToString(types::PhysicsAnimationMode mode);
        static types::PhysicsAnimationMode stringToPhysicsAnimationMode(const std::string& str);

        static json serializeDirectionalLight(const components::DirectionalLightComponent& light);
        static void deserializeDirectionalLight(const json& j, components::DirectionalLightComponent& light);

        static json serializePointLight(const components::PointLightComponent& light);
        static void deserializePointLight(const json& j, components::PointLightComponent& light);

        static json serializeSpotLight(const components::SpotLightComponent& light);
        static void deserializeSpotLight(const json& j, components::SpotLightComponent& light);

        static json serializePhysicsSettings(const types::PhysicsSettings& settings);
        static void deserializePhysicsSettings(const json& j, types::PhysicsSettings& settings);

        static json serializeAudioSettings(const types::AudioSettings& settings);
        static void deserializeAudioSettings(const json& j, types::AudioSettings& settings);

        static json serializeRenderSettings(const types::RenderSettings& settings);
        static void deserializeRenderSettings(const json& j, types::RenderSettings& settings);

        static std::string audioDistanceModelToString(types::AudioDistanceModel model);
        static types::AudioDistanceModel stringToAudioDistanceModel(const std::string& str);

        static std::string billboardSizeModeToString(components::BillboardSizeMode mode);
        static components::BillboardSizeMode stringToBillboardSizeMode(const std::string& str);

        static std::string billboardIconTypeToString(components::BillboardIconType type);
        static components::BillboardIconType stringToBillboardIconType(const std::string& str);

        static std::string rigidBodyTypeToString(components::RigidBodyType type);
        static components::RigidBodyType stringToRigidBodyType(const std::string& str);

        static std::string colliderShapeToString(components::ColliderShape shape);
        static components::ColliderShape stringToColliderShape(const std::string& str);

        static std::string shadowQualityToString(types::ShadowQuality quality);
        static types::ShadowQuality stringToShadowQuality(const std::string& str);

        static std::string cascadeSplitModeToString(types::CascadeSplitMode mode);
        static types::CascadeSplitMode stringToCascadeSplitMode(const std::string& str);

        static json serializePostProcessSettings(const postprocess::PostProcessSettings& settings);
        static void deserializePostProcessSettings(const json& j, postprocess::PostProcessSettings& settings);

        static std::string toneMappingModeToString(postprocess::ToneMappingMode mode);
        static postprocess::ToneMappingMode stringToToneMappingMode(const std::string& str);

        static json serializeTerrain(const components::TerrainComponent& terrain);
        static void deserializeTerrain(const json& j, components::TerrainComponent& terrain);

        static json serializeTerrainTile(const components::TerrainTileComponent& tile);
        static void deserializeTerrainTile(const json& j, components::TerrainTileComponent& tile);

        static json serializeGrass(const components::GrassComponent& grass);
        static void deserializeGrass(const json& j, components::GrassComponent& grass);

        static json serializeWater(const components::WaterComponent& water);
        static void deserializeWater(const json& j, components::WaterComponent& water);

        static json serializeWaterTile(const components::WaterTileComponent& tile);
        static void deserializeWaterTile(const json& j, components::WaterTileComponent& tile);

        static json serializeUICanvas(const components::UICanvasComponent& canvas);
        static void deserializeUICanvas(const json& j, components::UICanvasComponent& canvas);

        static json serializeUIRect(const components::UIRectComponent& rect);
        static void deserializeUIRect(const json& j, components::UIRectComponent& rect);

        static json serializeUIImage(const components::UIImageComponent& image);
        static void deserializeUIImage(const json& j, components::UIImageComponent& image);

        static json serializeUIScroll(const components::UIScrollComponent& scroll);
        static void deserializeUIScroll(const json& j, components::UIScrollComponent& scroll);

        static json serializeUILayoutGroup(const components::UILayoutGroupComponent& layoutGroup);
        static void deserializeUILayoutGroup(const json& j, components::UILayoutGroupComponent& layoutGroup);

        static json serializeUILabel(const components::UILabelComponent& label);
        static void deserializeUILabel(const json& j, components::UILabelComponent& label);

        static json serializeUIButton(const components::UIButtonComponent& button);
        static void deserializeUIButton(const json& j, components::UIButtonComponent& button);

        static json serializeUITextInput(const components::UITextInputComponent& textInput);
        static void deserializeUITextInput(const json& j, components::UITextInputComponent& textInput);

        static json serializeUICheckbox(const components::UICheckboxComponent& checkbox);
        static void deserializeUICheckbox(const json& j, components::UICheckboxComponent& checkbox);

        static json serializeUIDropdown(const components::UIDropdownComponent& dropdown);
        static void deserializeUIDropdown(const json& j, components::UIDropdownComponent& dropdown);

        static json serializeUITabs(const components::UITabsComponent& tabs);
        static void deserializeUITabs(const json& j, components::UITabsComponent& tabs);

        static json serializeUISlider(const components::UISliderComponent& slider);
        static void deserializeUISlider(const json& j, components::UISliderComponent& slider);

        static json serializeUIProgressBar(const components::UIProgressBarComponent& progressBar);
        static void deserializeUIProgressBar(const json& j, components::UIProgressBarComponent& progressBar);

        static json serializeSocketAttachment(const components::SocketAttachmentComponent& attachment);
        static void deserializeSocketAttachment(const json& j, components::SocketAttachmentComponent& attachment);

        static json serializeSocketOverride(const components::SocketOverrideComponent& override);
        static void deserializeSocketOverride(const json& j, components::SocketOverrideComponent& override);

        static json serializeNavmeshAgent(const components::NavmeshAgentComponent& agent);
        static void deserializeNavmeshAgent(const json& j, components::NavmeshAgentComponent& agent);

        static json serializeNavmesh(const components::NavmeshComponent& navmesh);
        static void deserializeNavmesh(const json& j, components::NavmeshComponent& navmesh);

        static json serializeRenderTexture(const components::RenderTextureComponent& rtt);
        static void deserializeRenderTexture(const json& j, components::RenderTextureComponent& rtt);

        static json serializeController(const components::ControllerComponent& controller);
        static void deserializeController(const json& j, components::ControllerComponent& controller);

        static json serializeIKTarget(const components::IKTargetComponent& ikTarget);
        static void deserializeIKTarget(const json& j, components::IKTargetComponent& ikTarget);

        static json serializeBehaviorTree(const components::BehaviorTreeComponent& bt);
        static void deserializeBehaviorTree(const json& j, components::BehaviorTreeComponent& bt);

        static json serializeDecal(const components::DecalComponent& decal);
        static void deserializeDecal(const json& j, components::DecalComponent& decal);

        static json serializeUIAnimation(const components::UIAnimationComponent& anim);
        static void deserializeUIAnimation(const json& j, components::UIAnimationComponent& anim);

        static json serializeUIMask(const components::UIMaskComponent& mask);
        static void deserializeUIMask(const json& j, components::UIMaskComponent& mask);

        static json serializeUIDraggable(const components::UIDraggableComponent& comp);
        static void deserializeUIDraggable(const json& j, components::UIDraggableComponent& comp);
        static json serializeUIDropTarget(const components::UIDropTargetComponent& comp);
        static void deserializeUIDropTarget(const json& j, components::UIDropTargetComponent& comp);

        static std::string updateModeToString(rendertexture::UpdateMode mode);
        static rendertexture::UpdateMode stringToUpdateMode(const std::string& str);

        static void resolveRenderTextureSourceNames();

        static std::string uiScaleModeToString(components::UIScaleMode mode);
        static components::UIScaleMode stringToUIScaleMode(const std::string& str);

        static std::string scrollbarVisibilityToString(components::ScrollbarVisibility visibility);
        static components::ScrollbarVisibility stringToScrollbarVisibility(const std::string& str);

        static std::string layoutDirectionToString(components::LayoutDirection direction);
        static components::LayoutDirection stringToLayoutDirection(const std::string& str);

        static std::string childAlignmentToString(components::ChildAlignment alignment);
        static components::ChildAlignment stringToChildAlignment(const std::string& str);

        static std::string horizontalAlignmentToString(components::HorizontalAlignment alignment);
        static components::HorizontalAlignment stringToHorizontalAlignment(const std::string& str);

        static std::string verticalAlignmentToString(components::VerticalAlignment alignment);
        static components::VerticalAlignment stringToVerticalAlignment(const std::string& str);

        static std::string textOverflowToString(components::TextOverflow overflow);
        static components::TextOverflow stringToTextOverflow(const std::string& str);

        static std::string fontStyleToString(components::FontStyle style);
        static components::FontStyle stringToFontStyle(const std::string& str);

        static PluginSerializeFn pluginSerializeHook;
        static PluginDeserializeFn pluginDeserializeHook;
    };
}
