#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"
#include <fstream>
#include <algorithm>

namespace serialization
{
    json SceneSerialization::serializeEntity(scene::Entity& entity)
    {
        json entityJson;

        entityJson["uuid"] = entity.getUUID().getValue();
        entityJson["name"] = entity.getName();

        if (entity.hasComponent<components::NameComponent>())
        {
            entityJson["isActive"] = entity.getComponent<components::NameComponent>().isActive;
        }

        if (entity.hasComponent<components::TransformComponent>())
        {
            entityJson["transform"] = serializeTransform(entity.getComponent<components::TransformComponent>());
        }

        entityJson["components"] = serializeEntityComponents(entity);

        json childrenJson = json::array();
        for (auto& child : entity.getChildren())
        {
            childrenJson.push_back(serializeEntity(child));
        }
        entityJson["children"] = childrenJson;

        return entityJson;
    }

    void SceneSerialization::deserializeChildren(const json& childrenJson, scene::Entity& parent,
                                                 scene::SceneGraphSystem& sceneGraph,
                                                 SceneLoadProgressCallback progressCallback, size_t& entitiesLoaded,
                                                 size_t totalEntities)
    {
        for (const auto& childJson : childrenJson)
        {
            if (!childJson.is_object())
            {
                vfLogWarning("Skipping invalid child entry in scene file (not an object)");
                continue;
            }

            std::string childName = childJson.value("name", "Unnamed");

            scene::Entity child(childName);

            if (!child.isValid())
            {
                vfLogError("Failed to create child entity '{}' during scene load", childName);
                continue;
            }

            if (childJson.contains("uuid") && childJson["uuid"].is_number_unsigned())
            {
                uint64_t uuidValue = childJson["uuid"].get<uint64_t>();
                child.addOrReplaceComponent<components::UUIDComponent>(uuidValue);
            }

            sceneGraph.addChild(parent, child);

            deserializeEntity(childJson, child, sceneGraph, false, progressCallback, entitiesLoaded, totalEntities);
        }
    }

    void SceneSerialization::deserializeEntity(const json& entityJson, scene::Entity& entity,
                                               scene::SceneGraphSystem& sceneGraph, bool isRoot,
                                               SceneLoadProgressCallback progressCallback, size_t& entitiesLoaded,
                                               size_t totalEntities)
    {
        std::string entityName = "Unnamed";
        if (entityJson.contains("name"))
        {
            entityName = entityJson["name"].get<std::string>();
            entity.setName(entityName);
        }

        if (entityJson.contains("isActive") && entityJson["isActive"].is_boolean())
        {
            if (entity.hasComponent<components::NameComponent>())
            {
                entity.getComponent<components::NameComponent>().isActive = entityJson["isActive"].get<bool>();
            }
        }

        if (progressCallback)
        {
            progressCallback(entityName, entitiesLoaded, totalEntities);
        }
        ++entitiesLoaded;

        if (isRoot && entityJson.contains("uuid"))
        {
            uint64_t uuidValue = entityJson["uuid"].get<uint64_t>();
            entity.addOrReplaceComponent<components::UUIDComponent>(uuidValue);
        }

        if (entityJson.contains("transform"))
        {
            auto& transform = entity.getComponent<components::TransformComponent>();
            deserializeTransform(entityJson["transform"], transform);
        }

        if (entityJson.contains("components"))
        {
            deserializeEntityComponents(entityJson["components"], entity);
        }

        if (entityJson.contains("children") && entityJson["children"].is_array())
        {
            deserializeChildren(entityJson["children"], entity, sceneGraph, progressCallback, entitiesLoaded,
                                totalEntities);
        }
    }

    scene::SceneGraphSystem SceneSerialization::loadScene(std::string_view filename)
    {
        scene::SceneGraphSystem sceneGraph;
        loadSceneInto(filename, sceneGraph);
        return sceneGraph;
    }

    size_t SceneSerialization::countEntities(const json& entityJson)
    {
        size_t count = 1;
        if (entityJson.contains("children") && entityJson["children"].is_array())
        {
            for (const auto& child : entityJson["children"])
            {
                count += countEntities(child);
            }
        }
        return count;
    }

    bool SceneSerialization::loadSceneInto(std::string_view filename, scene::SceneGraphSystem& sceneGraph,
                                           SceneLoadProgressCallback progressCallback)
    {
        json sceneJson;

        try
        {
            std::string filePath{filename};
            std::ifstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open file for reading: {}", filename);
                return false;
            }

            sceneJson = json::parse(file);
            file.close();

            if (!sceneJson.is_object())
            {
                vfLogError("Invalid scene file: root is not a JSON object");
                return false;
            }

            if (!sceneJson.contains("root") || !sceneJson["root"].is_object())
            {
                vfLogError("Invalid scene file: missing or invalid 'root' object");
                return false;
            }
        }
        catch (const json::parse_error& e)
        {
            vfLogError("JSON parse error while loading scene: {}", e.what());
            return false;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to read scene file: {}", e.what());
            return false;
        }

        try
        {
            size_t totalEntities = countEntities(sceneJson["root"]);
            size_t entitiesLoaded = 0;

            sceneGraph.clearScene();
            deserializeSceneSettings(sceneJson, sceneGraph);

            scene::Entity& root = sceneGraph.GetRoot();
            deserializeEntity(sceneJson["root"], root, sceneGraph, true, progressCallback, entitiesLoaded,
                              totalEntities);

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to deserialize scene: {}", e.what());
            sceneGraph.clearScene();
            return false;
        }
    }

    bool SceneSerialization::saveScene(scene::SceneGraphSystem& sceneGraph, std::string_view filename)
    {
        try
        {
            json sceneJson;
            sceneJson["version"] = "1.1";

            scene::Entity& root = sceneGraph.GetRoot();

            sceneJson["root"] = serializeEntity(root);
            sceneJson["physicsSettings"] = serializePhysicsSettings(sceneGraph.getPhysicsSettings());
            sceneJson["audioSettings"] = serializeAudioSettings(sceneGraph.getAudioSettings());
            sceneJson["renderSettings"] = serializeRenderSettings(sceneGraph.getRenderSettings());

            std::string filePath{filename};
            std::ofstream file{filePath};
            if (!file.is_open())
            {
                vfLogError("Failed to open file for writing: {}", filename);
                return false;
            }

            file << sceneJson.dump(2);
            file.close();

            vfLogInfo("Scene saved successfully to: {}", filename);
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to save scene: {}", e.what());
            return false;
        }
    }

    json SceneSerialization::createSnapshot(scene::SceneGraphSystem& sceneGraph)
    {
        try
        {
            json snapshot;
            snapshot["version"] = "1.1";

            scene::Entity& root = sceneGraph.GetRoot();
            snapshot["root"] = serializeEntity(root);
            snapshot["physicsSettings"] = serializePhysicsSettings(sceneGraph.getPhysicsSettings());
            snapshot["audioSettings"] = serializeAudioSettings(sceneGraph.getAudioSettings());
            snapshot["renderSettings"] = serializeRenderSettings(sceneGraph.getRenderSettings());

            return snapshot;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to create scene snapshot: {}", e.what());
            return json();
        }
    }

    bool SceneSerialization::restoreFromSnapshot(const json& snapshot, scene::SceneGraphSystem& sceneGraph)
    {
        try
        {
            if (!snapshot.is_object())
            {
                vfLogError("Invalid snapshot: not a JSON object");
                return false;
            }

            if (!snapshot.contains("root") || !snapshot["root"].is_object())
            {
                vfLogError("Invalid snapshot: missing or invalid 'root' object");
                return false;
            }

            sceneGraph.clearScene();
            deserializeSceneSettings(snapshot, sceneGraph);

            scene::Entity& root = sceneGraph.GetRoot();
            size_t entitiesLoaded = 0;
            size_t totalEntities = countEntities(snapshot["root"]);
            deserializeEntity(snapshot["root"], root, sceneGraph, true, nullptr, entitiesLoaded, totalEntities);

            vfLogInfo("Scene restored from snapshot successfully");
            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to restore scene from snapshot: {}", e.what());
            sceneGraph.clearScene();
            return false;
        }
    }
}
