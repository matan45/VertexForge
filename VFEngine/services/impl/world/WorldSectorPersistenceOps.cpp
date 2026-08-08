#include "WorldSectorServiceImpl.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include "world/WorldSectorSerialization.hpp"
#include "world/WorldDefinitionSerialization.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "../../events/render/ObjectStreamingEvents.hpp"
#include "../../events/render/LightStreamingEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "asset/AssetDatabase.hpp"
#include "resource/VirtualFileSystem.hpp"
#include "../common/ProjectPaths.hpp"
#include "print/Log.hpp"
#include <filesystem>
#include <chrono>
#include <sstream>
#include <iomanip>

namespace
{
    bool isManagedBySeparateSystem(const scene::Entity& entity)
    {
        return entity.hasComponent<components::TerrainComponent>()
            || entity.hasComponent<components::TerrainTileComponent>()
            || entity.hasComponent<components::OceanComponent>()
            || entity.hasComponent<components::IBLComponent>()
            || entity.hasComponent<components::CameraComponent>();
    }
}

namespace services
{
    bool WorldSectorServiceImpl::createWorld(const std::string& name, const std::string& filePath,
                                              const world::SectorConfig& sectorConfig,
                                              const world::SectorStreamingConfig& streamingConfig)
    {
        // Two domains, deliberately: currentWorldPath stays resolved because everything derived
        // from it does file IO, while the copy tagged onto the scene is project-relative so it
        // still names the right world on a player's machine.
        const std::string worldPath = resolveProjectPath(filePath);

        sectorManager.clear();
        sectorManager.setConfig(sectorConfig);

        worldDefinition = {};
        worldDefinition.name = name;
        worldDefinition.sectorConfig = sectorConfig;
        worldDefinition.streamingConfig = streamingConfig;

        streamer.setConfig(streamingConfig);
        streamer.setEnabled(true);

        worldMode = true;
        currentWorldPath = worldPath;

        // Enable GPU object streaming if configured
        if (streamingConfig.enableGPUObjectStreaming)
        {
            events::render::objectstreaming::SetObjectStreamingEnabledCommand cmd;
            cmd.enabled = true;
            ::events::EventDispatcher::instance().execute(cmd);
        }

        // Tag root entity so the world auto-loads with the scene
        auto& root = sceneGraph->GetRoot();
        root.addOrReplaceComponent<components::WorldSectorComponent>().worldFilePath =
            toProjectRelativePath(worldPath);

        // Assign existing entities to sectors (skip terrain/water/IBL/camera — they have their own systems)
        for (auto& child : root.getChildren())
        {
            if (isManagedBySeparateSystem(child))
                continue;

            if (child.hasComponent<components::TransformComponent>())
            {
                const auto& transform = child.getComponent<components::TransformComponent>();
                uint64_t uuid = child.getUUID().getValue();
                sectorManager.assignEntityToSector(uuid, transform.position);
            }
        }

        // Mark all sectors as Loaded since entities are already live in the scene,
        // and register their entities for GPU object/light streaming
        auto& registry = scene::EntityRegistry::getRegistry();
        sectorManager.forEachSector([&](world::WorldSector& sector)
        {
            sector.state = world::SectorState::Loaded;

            if (streamingConfig.enableGPUObjectStreaming)
            {
                std::vector<std::pair<uint64_t, entt::entity>> meshEntities;
                std::vector<uint32_t> lightEntityIds;

                for (uint64_t uuid : sector.entityUUIDs)
                {
                    auto ent = scene::EntityRegistry::findByUUID(uuid);
                    if (ent != entt::null)
                    {
                        // VK-1579: foliage is entity-free packed FoliageInstance data on
                        // TerrainTile and renders via the instanced collector — it is never
                        // an entity, so it never registers for per-entity GPU object streaming
                        // here (which would blow MAX_GPU_OBJECTS). Only real mesh entities do.
                        if (registry.any_of<components::MeshComponent>(ent))
                            meshEntities.emplace_back(uuid, ent);
                        if (registry.any_of<components::PointLightComponent, components::SpotLightComponent>(ent))
                            lightEntityIds.push_back(static_cast<uint32_t>(ent));
                    }
                }

                if (!meshEntities.empty())
                {
                    events::render::objectstreaming::RegisterSectorObjectsCommand cmd;
                    cmd.sectorId = world::sectorCoordToId(sector.coord);
                    cmd.entities = std::move(meshEntities);
                    ::events::EventDispatcher::instance().execute(cmd);
                }
                if (!lightEntityIds.empty())
                {
                    events::render::lightstreaming::RegisterSectorLightsCommand cmd;
                    cmd.sectorId = world::sectorCoordToId(sector.coord);
                    cmd.lightEntityIds = std::move(lightEntityIds);
                    ::events::EventDispatcher::instance().execute(cmd);
                }
            }
        });

        return saveWorld(filePath);
    }

    bool WorldSectorServiceImpl::saveWorld(const std::string& filePath)
    {
        if (!worldMode)
        {
            vfLogError("Cannot save world: not in world mode");
            return false;
        }

        std::string path = resolveProjectPath(filePath.empty() ? currentWorldPath : filePath);
        if (path.empty())
        {
            vfLogError("Cannot save world: no file path specified");
            return false;
        }

        auto& root = sceneGraph->GetRoot();
        root.addOrReplaceComponent<components::WorldSectorComponent>().worldFilePath =
            toProjectRelativePath(path);
        std::filesystem::path worldDir = std::filesystem::path(path).parent_path();
        std::filesystem::path sectorsDir = worldDir / "sectors";
        std::filesystem::create_directories(sectorsDir);

        std::vector<world::SectorCoord> staleHLODs;
        sectorManager.forEachSector([&](world::WorldSector& sector)
        {
            if (sector.dirty || sector.filePath.empty())
            {
                std::string sectorFileName = "sector_" +
                    std::to_string(sector.coord.x) + "_" +
                    std::to_string(sector.coord.z) + ".vfsector";
                std::string sectorPath = (sectorsDir / sectorFileName).string();

                bool contentChanged = sector.dirty;
                if (world::WorldSectorSerialization::saveSector(sector, sectorPath))
                {
                    sector.filePath = sectorPath;
                    // Only the serialized copy is relativized — sector.filePath keeps doing IO
                    worldDefinition.sectorFilePaths[sector.coord] = toProjectRelativePath(sectorPath);

                    // The baked HLOD no longer matches the saved content
                    if (contentChanged && !sector.hlodFilePath.empty())
                        staleHLODs.push_back(sector.coord);
                }
            }
        });

        for (const auto& coord : staleHLODs)
            invalidateHLODForSector(coord);

        currentWorldPath = path;
        bool result = world::WorldDefinitionSerialization::save(worldDefinition, path);

        if (result)
        {
            // Create or update .vfmeta sidecar for the .vfworld file
            auto metaPath = asset::AssetMetadataSerializer::getMetaPath(std::filesystem::path(path));
            auto existingMeta = asset::AssetMetadataSerializer::load(metaPath);

            asset::AssetMetadata metadata;
            metadata.guid = existingMeta.has_value() ? existingMeta->guid : asset::AssetGUID::generate();
            metadata.type = resource::AssetType::World;
            metadata.importSourcePath = path;
            {
                auto now = std::chrono::system_clock::now();
                auto time = std::chrono::system_clock::to_time_t(now);
                std::tm tm{};
                localtime_s(&tm, &time);
                std::ostringstream oss;
                oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
                metadata.importTimestamp = oss.str();
            }
            asset::AssetMetadataSerializer::save(metadata, metaPath);

            auto& db = asset::AssetDatabase::instance();
            if (!db.getGUID(path).has_value())
            {
                db.registerAssetWithGUID(metadata.guid, path, resource::AssetType::World);
            }
        }

        return result;
    }

    bool WorldSectorServiceImpl::loadWorld(const std::string& rawFilePath)
    {
        // The scene stores a project-relative world path; the file dialog hands back an absolute
        // one. Both land here, so resolve into the IO domain the rest of this function works in.
        const std::string filePath = resolveProjectPath(rawFilePath);

        world::WorldDefinition newDef;
        if (!world::WorldDefinitionSerialization::load(filePath, newDef))
            return false;

        sectorManager.clear();
        sectorManager.setConfig(newDef.sectorConfig);

        worldDefinition = newDef;
        streamer.setConfig(newDef.streamingConfig);
        streamer.setEnabled(true);
        hlodStreamer.setConfig(newDef.streamingConfig, newDef.hlodConfig);

        worldMode = true;
        currentWorldPath = filePath;

        // Enable GPU object streaming if configured
        if (newDef.streamingConfig.enableGPUObjectStreaming)
        {
            events::render::objectstreaming::SetObjectStreamingEnabledCommand cmd;
            cmd.enabled = true;
            ::events::EventDispatcher::instance().execute(cmd);
        }

        for (const auto& [coord, storedSectorPath] : worldDefinition.sectorFilePaths)
        {
            // Stored relative to the project; resolves to a loose file in the editor and to an
            // "Assets/..." archive key in a shipped game
            const std::string sectorPath = resolveProjectPath(storedSectorPath);

            auto& sector = sectorManager.getOrCreateSector(coord);
            sector.filePath = sectorPath;
            sector.state = world::SectorState::Unloaded;

            // Pre-cache metadata from binary header (44 bytes, fast)
            world::WorldSectorSerialization::readSectorMetadata(sectorPath, sector.metadata);

            // Restore the HLOD bake if one exists (the path isn't stored in the world
            // definition; GenerateHLODCommand uses this naming convention)
            std::string hlodPath = sectorPath;
            if (auto dotPos = hlodPath.rfind('.'); dotPos != std::string::npos)
                hlodPath = hlodPath.substr(0, dotPos);
            hlodPath += "_hlod0.vfHLOD";
            if (resource::VirtualFileSystem::instance().exists(hlodPath))
                sector.hlodFilePath = hlodPath;
        }

        ::events::world::WorldLoadedNotification notif;
        notif.worldPath = filePath;
        ::events::EventDispatcher::instance().publish(notif);

        return true;
    }

    bool WorldSectorServiceImpl::saveSector(const world::SectorCoord& coord, const std::string& filePath)
    {
        auto* sector = sectorManager.getSector(coord);
        if (!sector)
        {
            vfLogError("Cannot save sector ({},{}): not found", coord.x, coord.z);
            return false;
        }

        bool result = world::WorldSectorSerialization::saveSector(*sector, filePath);
        if (result)
        {
            worldDefinition.sectorFilePaths[coord] = toProjectRelativePath(filePath);
        }
        return result;
    }

    void WorldSectorServiceImpl::clearWorld()
    {
        if (!worldMode)
            return;

        // Disable GPU object streaming so entities render through the normal path again
        {
            events::render::objectstreaming::SetObjectStreamingEnabledCommand cmd;
            cmd.enabled = false;
            ::events::EventDispatcher::instance().execute(cmd);
        }

        // Drain all pending async sector loads before clearing
        pendingAsyncLoads.drain();

        entityLoader.clear();
        physicsSnapshots.clear();
        animationSnapshots.clear();
        vfxSnapshots.clear();
        audioSnapshots.clear();
        streamingSources.clear();
        streamingSourceOwners.clear();
        nextStreamingSourceId = 1;
        sectorManager.clear();

        auto& root = sceneGraph->GetRoot();
        if (root.hasComponent<components::WorldSectorComponent>())
        {
            root.removeComponent<components::WorldSectorComponent>();
        }

        hlodProxyManager.unloadAll(*sceneGraph);
        hlodStreamer.clear();
        hlodRegenQueue.clear();

        worldDefinition = {};
        streamer.setEnabled(false);
        worldMode = false;
        currentWorldPath.clear();
    }

} // namespace services
