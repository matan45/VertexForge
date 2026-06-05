#include "SceneSerialization.hpp"
#include "BinarySceneSerialization.hpp"
#include "../print/Log.hpp"
#include "JsonConverters.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../components/Components.hpp"
#include "../threading/JobSystem.hpp"
#include "../resource/VFSHelpers.hpp"
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
                                                 DeserializeEntityContext& ctx)
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

            ctx.sceneGraph.addChild(parent, child);

            DeserializeEntityContext childCtx{ctx.sceneGraph, false, ctx.progressCallback,
                                              ctx.entitiesLoaded, ctx.totalEntities};
            deserializeEntity(childJson, child, childCtx);
        }
    }

    void SceneSerialization::deserializeEntity(const json& entityJson, scene::Entity& entity,
                                               DeserializeEntityContext& ctx)
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

        if (ctx.progressCallback)
        {
            ctx.progressCallback(entityName, ctx.entitiesLoaded, ctx.totalEntities);
        }
        ++ctx.entitiesLoaded;

        if (ctx.isRoot && entityJson.contains("uuid"))
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
            deserializeChildren(entityJson["children"], entity, ctx);
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
        // Auto-detect binary scene format
        auto rawData = resource::readFileBytes(std::string(filename));
        if (BinarySceneSerialization::isBinaryScene(rawData))
        {
            return BinarySceneSerialization::loadBinarySceneInto(rawData, sceneGraph, filename, progressCallback);
        }

        json sceneJson;
        json settingsJson;

        try
        {
            if (!rawData.empty())
            {
                sceneJson = json::parse(rawData.begin(), rawData.end());
            }

            if (sceneJson.is_null())
            {
                vfLogError("Failed to open file for reading: {}", filename);
                return false;
            }

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

            if (!readLinkedSceneSettings(sceneJson, filename, settingsJson))
            {
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
            if (!deserializeSceneSettings(settingsJson, sceneGraph))
            {
                vfLogError("Failed to deserialize linked scene settings: {}", filename);
                sceneGraph.clearScene();
                return false;
            }

            scene::Entity& root = sceneGraph.GetRoot();
            DeserializeEntityContext ctx{sceneGraph, true, progressCallback, entitiesLoaded, totalEntities};
            deserializeEntity(sceneJson["root"], root, ctx);

            resolveRenderTextureSourceNames();

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to deserialize scene: {}", e.what());
            sceneGraph.clearScene();
            return false;
        }
    }

    bool SceneSerialization::loadSceneAdditive(std::string_view filename, scene::SceneGraphSystem& sceneGraph,
                                              scene::Entity& containerParent,
                                              SceneLoadProgressCallback progressCallback)
    {
        // Auto-detect binary scene format
        auto rawData = resource::readFileBytes(std::string(filename));
        if (BinarySceneSerialization::isBinaryScene(rawData))
        {
            return BinarySceneSerialization::loadBinarySceneAdditive(rawData, sceneGraph, containerParent,
                                                                    filename, progressCallback);
        }

        json sceneJson;

        try
        {
            if (!rawData.empty())
            {
                sceneJson = json::parse(rawData.begin(), rawData.end());
            }

            if (sceneJson.is_null())
            {
                vfLogError("Failed to open file for additive loading: {}", filename);
                return false;
            }

            if (!sceneJson.is_object())
            {
                vfLogError("Invalid scene file for additive load: root is not a JSON object");
                return false;
            }

            if (!sceneJson.contains("root") || !sceneJson["root"].is_object())
            {
                vfLogError("Invalid scene file for additive load: missing or invalid 'root' object");
                return false;
            }
        }
        catch (const json::parse_error& e)
        {
            vfLogError("JSON parse error while loading additive scene: {}", e.what());
            return false;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to read scene file for additive load: {}", e.what());
            return false;
        }

        try
        {
            const auto& rootJson = sceneJson["root"];

            // Skip scene settings (physics/audio/render) - those belong to the main scene.
            // Only deserialize entity children from the root into the container.

            if (rootJson.contains("children") && rootJson["children"].is_array())
            {
                size_t totalEntities = 0;
                for (const auto& childJson : rootJson["children"])
                {
                    totalEntities += countEntities(childJson);
                }

                size_t entitiesLoaded = 0;
                DeserializeEntityContext ctx{sceneGraph, false, progressCallback,
                                             entitiesLoaded, totalEntities};

                for (const auto& childJson : rootJson["children"])
                {
                    if (!childJson.is_object()) continue;

                    std::string childName = childJson.value("name", "Unnamed");
                    scene::Entity child(childName); // Generates new UUID

                    if (!child.isValid())
                    {
                        vfLogError("Failed to create entity '{}' during additive scene load", childName);
                        continue;
                    }

                    sceneGraph.addChild(containerParent, child);
                    deserializeEntity(childJson, child, ctx);
                }
            }

            // Also deserialize root-level components (IBL, camera, etc.) onto the container
            // if the source scene root had them
            if (rootJson.contains("components"))
            {
                deserializeEntityComponents(rootJson["components"], containerParent);
            }

            resolveRenderTextureSourceNames();

            return true;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to deserialize additive scene: {}", e.what());
            return false;
        }
    }

    json SceneSerialization::serializeRootEntity(scene::Entity& root)
    {
        auto children = root.getChildren();
        if (children.size() > 1)
        {
            json rootJson;
            rootJson["uuid"] = root.getUUID().getValue();
            rootJson["name"] = root.getName();

            if (root.hasComponent<components::NameComponent>())
            {
                rootJson["isActive"] = root.getComponent<components::NameComponent>().isActive;
            }
            if (root.hasComponent<components::TransformComponent>())
            {
                rootJson["transform"] = serializeTransform(root.getComponent<components::TransformComponent>());
            }
            rootJson["components"] = serializeEntityComponents(root);

            // Safe to parallelize: each child subtree is disjoint and serialization
            // is read-only (no structural registry mutations). Each thread reads only
            // its own subtree's components via entity handles.
            std::vector<std::future<json>> futures;
            futures.reserve(children.size());
            for (auto& child : children)
            {
                futures.push_back(threading::JobSystem::instance().submit(
                    [child]() mutable -> json
                    {
                        return serializeEntity(child);
                    }, threading::JobPriority::NORMAL
                ));
            }

            json childrenJson = json::array();
            for (auto& f : futures)
            {
                childrenJson.push_back(f.get());
            }
            rootJson["children"] = childrenJson;
            return rootJson;
        }

        return serializeEntity(root);
    }

    bool SceneSerialization::saveScene(scene::SceneGraphSystem& sceneGraph, std::string_view filename)
    {
        try
        {
            std::filesystem::path settingsPath = getSettingsPathForScene(filename);
            if (!saveSceneSettings(sceneGraph, settingsPath))
            {
                return false;
            }

            json sceneJson;
            sceneJson["version"] = "1.0";
            if (!writeSceneSettingsRef(sceneJson, filename, settingsPath))
            {
                return false;
            }
            sceneJson["root"] = serializeRootEntity(sceneGraph.GetRoot());

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
            snapshot["version"] = "1.0";
            snapshot["root"] = serializeRootEntity(sceneGraph.GetRoot());
            snapshot["physicsSettings"] = serializePhysicsSettings(sceneGraph.getPhysicsSettings());
            snapshot["audioSettings"] = serializeAudioSettings(sceneGraph.getAudioSettings());
            snapshot["renderSettings"] = serializeRenderSettings(sceneGraph.getRenderSettings());

            const auto& inputMappingPath = sceneGraph.getInputMappingPath();
            if (inputMappingPath.has_value() && !inputMappingPath->empty())
            {
                snapshot["inputMapping"] = *inputMappingPath;
            }

            return snapshot;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to create scene snapshot: {}", e.what());
            return json();
        }
    }

    json SceneSerialization::createSnapshot(scene::SceneGraphSystem& sceneGraph, std::string_view filename)
    {
        try
        {
            std::filesystem::path settingsPath = getSettingsPathForScene(filename);
            if (!saveSceneSettings(sceneGraph, settingsPath))
            {
                return json();
            }

            json snapshot;
            snapshot["version"] = "1.0";
            if (!writeSceneSettingsRef(snapshot, filename, settingsPath))
            {
                return json();
            }
            snapshot["root"] = serializeRootEntity(sceneGraph.GetRoot());

            return snapshot;
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to create scene snapshot: {}", e.what());
            return json();
        }
    }

    bool SceneSerialization::restoreFromSnapshot(const json& snapshot, scene::SceneGraphSystem& sceneGraph,
                                                  SceneLoadProgressCallback progressCallback)
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
            if (!deserializeSceneSettings(snapshot, sceneGraph))
            {
                vfLogError("Invalid scene settings in snapshot");
                sceneGraph.clearScene();
                return false;
            }

            scene::Entity& root = sceneGraph.GetRoot();
            size_t entitiesLoaded = 0;
            size_t totalEntities = countEntities(snapshot["root"]);
            DeserializeEntityContext ctx{sceneGraph, true, progressCallback, entitiesLoaded, totalEntities};
            deserializeEntity(snapshot["root"], root, ctx);

            resolveRenderTextureSourceNames();

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

    bool SceneSerialization::restoreFromSnapshot(const json& snapshot, scene::SceneGraphSystem& sceneGraph,
                                                  std::string_view filename,
                                                  SceneLoadProgressCallback progressCallback)
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

            json settingsJson;
            if (!readLinkedSceneSettings(snapshot, filename, settingsJson))
            {
                return false;
            }

            sceneGraph.clearScene();
            if (!deserializeSceneSettings(settingsJson, sceneGraph))
            {
                vfLogError("Invalid linked scene settings in snapshot");
                sceneGraph.clearScene();
                return false;
            }

            scene::Entity& root = sceneGraph.GetRoot();
            size_t entitiesLoaded = 0;
            size_t totalEntities = countEntities(snapshot["root"]);
            DeserializeEntityContext ctx{sceneGraph, true, progressCallback, entitiesLoaded, totalEntities};
            deserializeEntity(snapshot["root"], root, ctx);

            resolveRenderTextureSourceNames();

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
