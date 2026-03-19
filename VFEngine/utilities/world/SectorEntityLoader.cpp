#include "SectorEntityLoader.hpp"
#include "../serialization/SceneSerialization.hpp"
#include "../scene/SceneGraphSystem.hpp"
#include "../scene/Entity.hpp"
#include "../scene/EntityRegistry.hpp"
#include "../components/Components.hpp"
#include "../print/Log.hpp"
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace world
{
    void SectorEntityLoader::queueSectorLoadFromData(const SectorCoord& coord, const std::vector<std::pair<std::string, std::string>>& entityNamesAndJson)
    {
        for (const auto& [name, json] : entityNamesAndJson)
        {
            PendingLoad load;
            load.coord = coord;
            load.entityName = name;
            load.rawJson = json;
            pendingLoads.push_back(std::move(load));
        }
    }

    void SectorEntityLoader::queueSectorUnload(const SectorCoord& coord, const std::vector<uint64_t>& uuids)
    {
        for (uint64_t uuid : uuids)
        {
            pendingUnloads.push_back({coord, uuid});
        }
    }

    void SectorEntityLoader::cancelPendingLoads(const SectorCoord& coord)
    {
        std::deque<PendingLoad> remaining;
        for (auto& load : pendingLoads)
        {
            if (!(load.coord == coord))
                remaining.push_back(std::move(load));
        }
        pendingLoads = std::move(remaining);
    }

    void SectorEntityLoader::update(scene::SceneGraphSystem& sceneGraph, int maxEntitiesPerFrame)
    {
        int processed = 0;

        while (!pendingUnloads.empty() && processed < maxEntitiesPerFrame)
        {
            auto pending = pendingUnloads.front();
            pendingUnloads.pop_front();

            auto& registry = scene::EntityRegistry::getRegistry();
            auto uuidView = registry.view<components::UUIDComponent>();
            bool found = false;

            for (auto entity : uuidView)
            {
                const auto& uuidComp = uuidView.get<components::UUIDComponent>(entity);
                if (uuidComp.id.getValue() == pending.uuid)
                {
                    scene::Entity sceneEntity(entity);

                    if (onEntityPreDestroy)
                    {
                        onEntityPreDestroy(static_cast<uint64_t>(static_cast<uint32_t>(entity)));
                    }

                    sceneGraph.removeEntity(sceneEntity);

                    if (onEntityUnloaded)
                    {
                        onEntityUnloaded(pending.uuid, pending.coord);
                    }

                    found = true;
                    break;
                }
            }

            if (!found)
            {
                vfLogWarning("Sector unload: entity UUID {} not found in registry", pending.uuid);
            }

            ++processed;
        }

        std::unordered_set<uint64_t> loadedThisFrame;
        while (!pendingLoads.empty() && processed < maxEntitiesPerFrame)
        {
            auto pending = std::move(pendingLoads.front());
            pendingLoads.pop_front();

            try
            {
                nlohmann::json entityJson = nlohmann::json::parse(pending.rawJson);

                if (entityJson.contains("uuid") && entityJson["uuid"].is_number_unsigned())
                {
                    uint64_t uuidValue = entityJson["uuid"].get<uint64_t>();

                    if (loadedThisFrame.contains(uuidValue))
                    {
                        ++processed;
                        continue;
                    }

                    auto& reg = scene::EntityRegistry::getRegistry();
                    auto uuidView = reg.view<components::UUIDComponent>();
                    bool alreadyExists = false;
                    for (auto ent : uuidView)
                    {
                        if (uuidView.get<components::UUIDComponent>(ent).id.getValue() == uuidValue)
                        {
                            alreadyExists = true;
                            break;
                        }
                    }
                    if (alreadyExists)
                    {
                        ++processed;
                        continue;
                    }
                }

                scene::Entity newEntity(pending.entityName);

                if (!newEntity.isValid())
                {
                    vfLogError("Failed to create entity '{}' during sector load", pending.entityName);
                    ++processed;
                    continue;
                }

                if (entityJson.contains("uuid") && entityJson["uuid"].is_number_unsigned())
                {
                    uint64_t uuidValue = entityJson["uuid"].get<uint64_t>();
                    newEntity.addOrReplaceComponent<components::UUIDComponent>(uuidValue);
                }

                sceneGraph.addChild(sceneGraph.GetRoot(), newEntity);

                size_t entitiesLoaded = 0;
                size_t totalEntities = 1;
                serialization::DeserializeEntityContext ctx{sceneGraph, false, nullptr,
                                                            entitiesLoaded, totalEntities};
                serialization::SceneSerialization::deserializeEntity(
                    entityJson, newEntity, ctx);

                uint64_t uuid = newEntity.getUUID().getValue();
                loadedThisFrame.insert(uuid);

                if (onEntityPostLoad)
                {
                    std::string meshPath;
                    std::string animatorPath;
                    if (newEntity.hasComponent<components::MeshComponent>())
                    {
                        const auto& meshComp = newEntity.getComponent<components::MeshComponent>();
                        meshPath = meshComp.meshRef.resolve();
                        animatorPath = meshComp.animatorRef.resolve();
                    }
                    onEntityPostLoad(uuid, meshPath, animatorPath);
                }

                if (onEntityLoaded)
                {
                    onEntityLoaded(uuid, pending.coord);
                }
            }
            catch (const std::exception& e)
            {
                vfLogError("Failed to load entity during sector streaming: {}", e.what());
            }

            ++processed;
        }
    }

} // namespace world
