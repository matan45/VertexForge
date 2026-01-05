#pragma once
#include <string_view>
#include <functional>
#include <nlohmann/json.hpp>
#include "../scene/Entity.hpp"

namespace scene
{
    class SceneGraphSystem;
}

namespace serialization
{
    using json = nlohmann::json;

    // Progress callback for scene loading
    // Parameters: currentEntityName, entitiesLoaded, totalEntities
    using SceneLoadProgressCallback = std::function<void(const std::string&, size_t, size_t)>;

    class PrefabSerialization;

    class SceneSerialization
    {
        friend class PrefabSerialization;

    public:
        static scene::SceneGraphSystem loadScene(std::string_view filename);
        static bool loadSceneInto(std::string_view filename, scene::SceneGraphSystem& sceneGraph,
                                  SceneLoadProgressCallback progressCallback = nullptr);
        static bool saveScene(scene::SceneGraphSystem& sceneGraph, std::string_view filename);

        // In-memory snapshot methods for play mode state capture/restore
        static json createSnapshot(scene::SceneGraphSystem& sceneGraph);
        static bool restoreFromSnapshot(const json& snapshot, scene::SceneGraphSystem& sceneGraph);

        // Count entities in JSON for progress tracking


    private:
        static json serializeEntity(scene::Entity& entity);
        static void deserializeEntity(const json& entityJson, scene::Entity& entity,
                                      scene::SceneGraphSystem& sceneGraph, bool isRoot,
                                      SceneLoadProgressCallback progressCallback, size_t& entitiesLoaded,
                                      size_t totalEntities);

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

        static json serializeAudioSource2D(const components::AudioSource2DComponent& audioSource);
        static void deserializeAudioSource2D(const json& j, components::AudioSource2DComponent& audioSource);

        static json serializeAudioSource3D(const components::AudioSource3DComponent& audioSource);
        static void deserializeAudioSource3D(const json& j, components::AudioSource3DComponent& audioSource);

        static json serializeScript(const components::ScriptComponent& script);
        static void deserializeScript(const json& j, components::ScriptComponent& script);
    };
}
