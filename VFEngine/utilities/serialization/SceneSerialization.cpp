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
        return serializeEntityImpl(entity, false);
    }

    json SceneSerialization::serializeEntityImpl(scene::Entity& entity, bool parallelChildren)
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

        auto children = entity.getChildren();
        json childrenJson = json::array();

        // List-view item instances are engine-managed (rebuilt from the item template on
        // load) — baking them would duplicate items, so they are skipped at every level.
        if (parallelChildren && children.size() > 1)
        {
            // Each child subtree is disjoint and serialization is read-only (no structural
            // registry mutation). Serialize subtrees concurrently and collect in child order
            // so the result is byte-identical to the serial path. Subtrees are serialized
            // serially inside each job (no nested submission -> no worker-thread deadlock).
            std::vector<std::future<json>> futures;
            futures.reserve(children.size());
            for (auto& child : children)
            {
                if (child.hasComponent<components::UIListItemComponent>())
                    continue;
                // VK-1435 / VK-1433 Phase 4 — the UI Layer Builder's and Prefab Rig Preview's
                // tagged sandbox subtrees are editor-only and must never bake into the saved scene.
                if (child.hasComponent<components::UIPreviewTagComponent>() ||
                    child.hasComponent<components::PreviewSandboxTagComponent>())
                    continue;
                futures.push_back(threading::JobSystem::instance().submit(
                    [child]() mutable -> json { return serializeEntity(child); },
                    threading::JobPriority::NORMAL));
            }
            for (auto& f : futures)
                childrenJson.push_back(f.get());
        }
        else
        {
            for (auto& child : children)
            {
                if (child.hasComponent<components::UIListItemComponent>())
                    continue;
                // VK-1435 / VK-1433 Phase 4 — the UI Layer Builder's and Prefab Rig Preview's
                // tagged sandbox subtrees are editor-only and must never bake into the saved scene.
                if (child.hasComponent<components::UIPreviewTagComponent>() ||
                    child.hasComponent<components::PreviewSandboxTagComponent>())
                    continue;
                childrenJson.push_back(serializeEntity(child));
            }
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

    void SceneSerialization::deserializeEntitySelf(const json& entityJson, scene::Entity& entity,
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
    }

    void SceneSerialization::deserializeEntity(const json& entityJson, scene::Entity& entity,
                                               DeserializeEntityContext& ctx)
    {
        deserializeEntitySelf(entityJson, entity, ctx);

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

    namespace
    {
        // Enqueue an entity's children in reverse so the work stack pops them in
        // original array order -> identical DFS pre-order to the recursive loader.
        void pushChildrenReversed(IncrementalLoadState& state, const scene::Entity& parent,
                                  const json& parentJson)
        {
            if (!parentJson.contains("children") || !parentJson["children"].is_array())
            {
                return;
            }
            const json& children = parentJson["children"];
            for (auto it = children.rbegin(); it != children.rend(); ++it)
            {
                state.stack.push_back(IncrementalLoadState::WorkItem{parent, &(*it)});
            }
        }
    }

    bool SceneSerialization::beginIncrementalLoad(std::string_view filename,
                                                  scene::SceneGraphSystem& sceneGraph,
                                                  SceneLoadProgressCallback progressCallback,
                                                  IncrementalLoadState& state)
    {
        state = IncrementalLoadState{};
        state.sceneGraph = &sceneGraph;
        state.progressCallback = progressCallback;

        auto rawData = resource::readFileBytes(std::string(filename));

        // Binary scenes have no incremental path yet - load synchronously.
        if (BinarySceneSerialization::isBinaryScene(rawData))
        {
            state.success =
                BinarySceneSerialization::loadBinarySceneInto(rawData, sceneGraph, filename, progressCallback);
            state.finished = true;
            return false;
        }

        json settingsJson;
        try
        {
            if (!rawData.empty())
            {
                state.sceneJson = json::parse(rawData.begin(), rawData.end());
            }

            if (state.sceneJson.is_null() || !state.sceneJson.is_object() ||
                !state.sceneJson.contains("root") || !state.sceneJson["root"].is_object())
            {
                vfLogError("Invalid scene file (incremental load): {}", filename);
                state.finished = true;
                return false;
            }

            if (!readLinkedSceneSettings(state.sceneJson, filename, settingsJson))
            {
                state.finished = true;
                return false;
            }
        }
        catch (const std::exception& e)
        {
            vfLogError("JSON parse error during incremental scene load: {}", e.what());
            state.finished = true;
            return false;
        }

        try
        {
            state.totalEntities = countEntities(state.sceneJson["root"]);

            sceneGraph.clearScene();
            if (!deserializeSceneSettings(settingsJson, sceneGraph))
            {
                vfLogError("Failed to deserialize linked scene settings (incremental): {}", filename);
                sceneGraph.clearScene();
                state.finished = true;
                return false;
            }

            const json& rootJson = state.sceneJson["root"];
            scene::Entity& root = sceneGraph.GetRoot();
            DeserializeEntityContext ctx{sceneGraph, true, progressCallback,
                                         state.entitiesLoaded, state.totalEntities};
            deserializeEntitySelf(rootJson, root, ctx);

            pushChildrenReversed(state, root, rootJson);

            if (state.stack.empty())
            {
                resolveRenderTextureSourceNames();
                state.finished = true;
                state.success = true;
                return false;
            }
            return true; // stepping required
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed to begin incremental scene load: {}", e.what());
            sceneGraph.clearScene();
            state.finished = true;
            return false;
        }
    }

    bool SceneSerialization::stepIncrementalLoad(IncrementalLoadState& state, int maxEntitiesPerFrame)
    {
        if (state.finished || !state.sceneGraph)
        {
            return false;
        }

        const int budget = maxEntitiesPerFrame > 0 ? maxEntitiesPerFrame : 1;
        int processed = 0;

        try
        {
            while (!state.stack.empty() && processed < budget)
            {
                IncrementalLoadState::WorkItem item = state.stack.back();
                state.stack.pop_back();

                const json& childJson = *item.childJson;
                if (!childJson.is_object())
                {
                    vfLogWarning("Skipping invalid child entry in scene file (not an object)");
                    continue;
                }

                std::string childName = childJson.value("name", "Unnamed");
                scene::Entity child(childName);
                if (!child.isValid())
                {
                    vfLogError("Failed to create child entity '{}' during incremental scene load", childName);
                    continue;
                }

                if (childJson.contains("uuid") && childJson["uuid"].is_number_unsigned())
                {
                    uint64_t uuidValue = childJson["uuid"].get<uint64_t>();
                    child.addOrReplaceComponent<components::UUIDComponent>(uuidValue);
                }

                scene::Entity parent = item.parent;
                state.sceneGraph->addChild(parent, child);

                DeserializeEntityContext ctx{*state.sceneGraph, false, state.progressCallback,
                                             state.entitiesLoaded, state.totalEntities};
                deserializeEntitySelf(childJson, child, ctx);

                pushChildrenReversed(state, child, childJson);

                ++processed;
            }
        }
        catch (const std::exception& e)
        {
            vfLogError("Failed during incremental scene load step: {}", e.what());
            state.stack.clear();
            state.sceneGraph->clearScene();
            state.finished = true;
            state.success = false;
            return false;
        }

        if (state.stack.empty())
        {
            resolveRenderTextureSourceNames();
            state.finished = true;
            state.success = true;
            return false;
        }
        return true;
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
        // Parallelize across the root's direct children (the top-level scene entities).
        return serializeEntityImpl(root, true);
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
