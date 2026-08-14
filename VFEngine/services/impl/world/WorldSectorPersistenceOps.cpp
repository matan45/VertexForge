#include "WorldSectorServiceImpl.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "components/Components.hpp"
#include "world/WorldSectorSerialization.hpp"
#include "world/WorldDefinitionSerialization.hpp"
#include "world/SectorAssignment.hpp"
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

        // VK-1591: same coord-collision hazard as loadWorld — never carry blobs across worlds
        // VK-1592: drainSectorLoads, not a bare AsyncLoadQueue::drain — a request still queued in
        // the scheduler has no worker behind it and would block the main thread forever.
        drainSectorLoads();
        drainHlodLoads();
        clearPrefetchedBlobs();

        // VK-1599: a world is created with exactly one grid. Extra grids are added afterwards from
        // the World Sectors window - the creation wizard stays a single-grid affair, which is also
        // what keeps a brand-new world's .vfworld free of a "grids" key.
        worldDefinition = {};
        worldDefinition.name = name;
        worldDefinition.primaryGrid().sectorConfig = sectorConfig;
        worldDefinition.primaryGrid().streamingConfig = streamingConfig;

        // Rebuilds the runtime list from the definition above, clearing every manager on the way.
        rebuildGridRuntimes();

        // VK-1595: a new world starts from its own config - a session override tuned against the
        // previous world must not silently govern this one.
        resetStreamingSessionState();
        applyEffectiveStreamingConfig();
        primaryRuntime().streamer.setEnabled(true);

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

        // Assign existing entities to sectors. The decision skips terrain/water/IBL/camera (they
        // have their own systems) and, since VK-1597, anything the user pinned as not spatially
        // loaded — those stay scene-graph children and are persisted by the scene file.
        for (auto& child : root.getChildren())
        {
            // A brand-new world has exactly one grid, so every spatial entity lands on it - but
            // route through the grid-aware resolver anyway, so an entity carrying a
            // StreamingPolicyComponent from a previous world does not silently resolve against
            // the wrong sector size.
            const auto assignment = resolveEntityAssignment(child);
            if (!assignment.isSpatial())
                continue;

            gridRuntime(assignment.gridIndex)
                .manager.assignEntityToSector(child.getUUID().getValue(), assignment.coord);
        }

        // Mark all sectors as Loaded since entities are already live in the scene,
        // and register their entities for GPU object/light streaming
        auto& registry = scene::EntityRegistry::getRegistry();
        for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
        {
        gridRuntime(gridIndex).manager.forEachSector([&](world::WorldSector& sector)
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
                    cmd.sectorId = world::sectorRegistrationId(gridIndex, sector.coord);
                    cmd.entities = std::move(meshEntities);
                    ::events::EventDispatcher::instance().execute(cmd);
                }
                if (!lightEntityIds.empty())
                {
                    events::render::lightstreaming::RegisterSectorLightsCommand cmd;
                    cmd.sectorId = world::sectorRegistrationId(gridIndex, sector.coord);
                    cmd.lightEntityIds = std::move(lightEntityIds);
                    ::events::EventDispatcher::instance().execute(cmd);
                }
            }
        });
        }

        // VK-1590: the scene being converted is already fully populated, so its cross-entity
        // references never pass through the sector spawn path. Register them once here.
        rescanEntityReferences();

        return saveWorld(filePath);
    }

    bool WorldSectorServiceImpl::saveWorld(const std::string& filePath)
    {
        if (!worldMode)
        {
            vfLogError("Cannot save world: not in world mode");
            return false;
        }

        // VK-1594: the async HLOD bake holds an immutable snapshot of the sector file list and its
        // workers are reading those .vfsector files right now. Saving would rewrite them mid-read
        // and produce proxies baked from a mix of old and new geometry, so refuse rather than try
        // to make the two concurrent. The bake is an explicit, cancellable user action.
        if (hlodBaker.isRunning())
        {
            vfLogError("Cannot save world: an HLOD bake is in progress (cancel it first)");
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

        // VK-1597: the authoritative half of the streaming-policy migration, and the ticket's own
        // "migrates it out on next Save World". The notification path only covers the inspector
        // checkbox; the component can also arrive via prefab instantiation, a script, or undo, and
        // EntityCreatedNotification fires on a bare entity before its components are deserialized.
        // Runs BEFORE the save loop so the dirtied sectors it produces are written this pass.
        alwaysLoadedMigrationCount += reconcileAlwaysLoadedEntities();

        // VK-1599: every grid writes into the same sectors/ directory; the filenames are what keep
        // them apart (world::sectorFileName prefixes grids 1+ with g<N>_). Only the PRIMARY grid
        // can have HLOD bakes to invalidate.
        std::vector<world::SectorCoord> staleHLODs;
        for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
        {
            auto& gridPaths = worldDefinition.grid(gridIndex).sectorFilePaths;

            gridRuntime(gridIndex).manager.forEachSector([&](world::WorldSector& sector)
            {
                // VK-1591: a Prefetching/Prefetched sector holds bytes, not entities. Its
                // entityUUIDs is deliberately empty, and saveSector rebuilds content by resolving
                // those UUIDs against EntityRegistry - saving one would overwrite a good .vfsector
                // with an empty one. Load-bearing, not belt-and-braces: assignEntityToSector
                // dirties a sector unconditionally, so a dynamic entity wandering into a
                // prefetched coord would otherwise pull it into this loop.
                if (sector.state == world::SectorState::Prefetching ||
                    sector.state == world::SectorState::Prefetched)
                    return;

                if (sector.dirty || sector.filePath.empty())
                {
                    std::string sectorPath =
                        (sectorsDir / world::sectorFileName(gridIndex, sector.coord)).string();

                    bool contentChanged = sector.dirty;
                    if (world::WorldSectorSerialization::saveSector(sector, sectorPath))
                    {
                        sector.filePath = sectorPath;
                        // Only the serialized copy is relativized — sector.filePath keeps doing IO
                        gridPaths[sector.coord] = toProjectRelativePath(sectorPath);

                        // The baked HLOD no longer matches the saved content
                        if (contentChanged && !sector.hlodFilePath.empty() &&
                            gridIndex == world::kPrimaryGridIndex)
                        {
                            staleHLODs.push_back(sector.coord);
                        }
                    }
                }
            });
        }

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

        // VK-1591: prefetch blobs are keyed on a bare SectorCoord, so without this the new world's
        // (0,0) would activate the previous world's cached bytes. Drain first — an in-flight read
        // would otherwise land in a later poll and be attributed to the new world's sector.
        // VK-1592: drainSectorLoads (not a bare drain) or an undispatched request hangs the main
        // thread; drainHlodLoads applies the same cross-world argument to .vfHLOD proxy reads.
        drainSectorLoads();
        drainHlodLoads();
        clearPrefetchedBlobs();

        worldDefinition = newDef;
        // VK-1599: one runtime per grid the file declares, each configured from its own block.
        rebuildGridRuntimes();

        // VK-1595: same reasoning as createWorld - the loaded world governs itself, so any session
        // override tuned against the previous one is dropped and the streamer is unpaused.
        resetStreamingSessionState();
        applyEffectiveStreamingConfig();
        for (auto& grid : grids)
            grid->streamer.setEnabled(true);

        worldMode = true;
        currentWorldPath = filePath;

        // Enable GPU object streaming if ANY grid asks for it: the renderer-side toggle is global,
        // so the honest reading of N per-grid flags is their OR.
        bool wantsGPUObjectStreaming = false;
        for (const auto& grid : worldDefinition.grids)
            wantsGPUObjectStreaming = wantsGPUObjectStreaming || grid.streamingConfig.enableGPUObjectStreaming;

        if (wantsGPUObjectStreaming)
        {
            events::render::objectstreaming::SetObjectStreamingEnabledCommand cmd;
            cmd.enabled = true;
            ::events::EventDispatcher::instance().execute(cmd);
        }

        for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
        {
        auto& manager = gridRuntime(gridIndex).manager;
        for (const auto& [coord, storedSectorPath] : worldDefinition.grid(gridIndex).sectorFilePaths)
        {
            // Stored relative to the project; resolves to a loose file in the editor and to an
            // "Assets/..." archive key in a shipped game
            const std::string sectorPath = resolveProjectPath(storedSectorPath);

            auto& sector = manager.getOrCreateSector(coord);
            sector.filePath = sectorPath;
            sector.state = world::SectorState::Unloaded;

            // Pre-cache metadata from binary header (44 bytes, fast)
            world::WorldSectorSerialization::readSectorMetadata(sectorPath, sector.metadata);

            // Restore the HLOD bake if one exists (the path isn't stored in the world
            // definition; GenerateHLODCommand uses this naming convention).
            //
            // VK-1599: the primary grid only. HLOD is bound to it, so a grid-1 sector can never
            // have a bake - probing would be a pointless VFS lookup per sector, and a stray
            // sector_g1_..._hlod0.vfHLOD left behind by hand would be adopted as if it were real.
            if (gridIndex != world::kPrimaryGridIndex)
                continue;

            std::string hlodPath = sectorPath;
            if (auto dotPos = hlodPath.rfind('.'); dotPos != std::string::npos)
                hlodPath = hlodPath.substr(0, dotPos);
            hlodPath += "_hlod0.vfHLOD";
            if (resource::VirtualFileSystem::instance().exists(hlodPath))
                sector.hlodFilePath = hlodPath;
        }
        }

        // VK-1590: register references held by entities that were already in the scene before
        // the world opened — they never go through the sector spawn path.
        rescanEntityReferences();

        ::events::world::WorldLoadedNotification notif;
        notif.worldPath = filePath;
        ::events::EventDispatcher::instance().publish(notif);

        return true;
    }

    bool WorldSectorServiceImpl::saveSector(uint8_t gridIndex, const world::SectorCoord& coord,
                                            const std::string& filePath)
    {
        auto* sector = gridRuntime(gridIndex).manager.getSector(coord);
        if (!sector)
        {
            vfLogError("Cannot save sector ({},{}) on grid {}: not found",
                       coord.x, coord.z, static_cast<int>(gridIndex));
            return false;
        }

        // VK-1591: same data-loss guard as saveWorld — a prefetched sector has no live entities
        // to serialize, so writing it would truncate the file to an empty entity array.
        if (sector->state == world::SectorState::Prefetching ||
            sector->state == world::SectorState::Prefetched)
        {
            vfLogWarning("Skipping save of prefetched sector ({},{}): it holds no entities",
                         coord.x, coord.z);
            return false;
        }

        bool result = world::WorldSectorSerialization::saveSector(*sector, filePath);
        if (result)
        {
            worldDefinition.grid(gridIndex).sectorFilePaths[coord] = toProjectRelativePath(filePath);
        }
        return result;
    }

    uint8_t WorldSectorServiceImpl::addGrid(const std::string& name,
                                            const world::SectorConfig& sectorConfig,
                                            const world::SectorStreamingConfig& streamingConfig)
    {
        if (worldDefinition.grids.size() >= world::kMaxGrids)
        {
            vfLogError("Cannot add grid '{}': a world may declare at most {} grids.",
                       name, static_cast<int>(world::kMaxGrids));
            return world::kMaxGrids;
        }

        world::GridDefinition definition;
        definition.name = name.empty() ? "Grid" : name;
        definition.sectorConfig = sectorConfig;
        definition.streamingConfig = streamingConfig;
        world::normalizeStreamingConfig(definition.streamingConfig);
        worldDefinition.grids.push_back(std::move(definition));

        // Appending a runtime rather than rebuilding: the other grids may have sectors resident and
        // reads in flight, and rebuildGridRuntimes deliberately keeps nothing. `grids` holds
        // unique_ptrs precisely so this push_back cannot move the live GridRuntimes.
        auto runtime = std::make_unique<GridRuntime>();
        runtime->manager.setConfig(worldDefinition.grids.back().sectorConfig);
        runtime->streamer.setConfig(worldDefinition.grids.back().streamingConfig);
        runtime->streamer.setEnabled(worldMode);
        grids.push_back(std::move(runtime));
        perGridActions.resize(grids.size());

        return static_cast<uint8_t>(grids.size() - 1);
    }

    std::string WorldSectorServiceImpl::removeGrid(uint8_t gridIndex)
    {
        if (gridIndex == world::kPrimaryGridIndex)
            return "The Default grid cannot be removed - terrain, ocean, navmesh and HLOD are bound to it.";
        if (gridIndex >= gridCount())
            return "No such grid.";
        if (isPlayMode)
            return "Stop play mode before removing a grid.";

        // Refuse rather than silently orphan: the .vfsector files are the only copy of those
        // entities' data. Re-assign them to another grid (or delete them) first.
        if (!worldDefinition.grid(gridIndex).sectorFilePaths.empty())
        {
            return "This grid still owns sector files. Move its entities to another grid and "
                   "Save World first.";
        }

        bool hasEntities = false;
        gridRuntime(gridIndex).manager.forEachSector([&](const world::WorldSector& sector)
        {
            if (!sector.entityUUIDs.empty())
                hasEntities = true;
        });
        if (hasEntities)
            return "This grid still has resident entities. Move them to another grid first.";

        // Drop the runtime before the definition so nothing can index past the end in between.
        grids.erase(grids.begin() + gridIndex);
        worldDefinition.grids.erase(worldDefinition.grids.begin() + gridIndex);
        perGridActions.resize(grids.size());

        // Every entity on a HIGHER grid has just had its index shifted down by one. Their
        // components still name the old index, so re-resolve them all rather than letting
        // clampGridIndex quietly fold the last grid's entities onto the primary one.
        auto& root = sceneGraph->GetRoot();
        for (auto& child : root.getChildren())
        {
            if (!child.hasComponent<components::StreamingPolicyComponent>())
                continue;

            auto& policy = child.getComponent<components::StreamingPolicyComponent>();
            if (policy.gridIndex > gridIndex)
                --policy.gridIndex;
        }

        return {};
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

        // Drain all pending async sector and HLOD loads before clearing
        drainSectorLoads(); // VK-1592: cancels undispatched scheduler requests first
        drainHlodLoads();
        clearPrefetchedBlobs(); // VK-1591

        entityLoader.clear();
        // VK-1590: otherwise the ledger keeps entries keyed on the previous world's UUIDs.
        referenceResolver.clear();
        refTargetProbeQueue.clear();
        physicsSnapshots.clear();
        animationSnapshots.clear();
        vfxSnapshots.clear();
        audioSnapshots.clear();
        clearStreamingSources();
        for (auto& grid : grids)
            grid->manager.clear();

        auto& root = sceneGraph->GetRoot();
        if (root.hasComponent<components::WorldSectorComponent>())
        {
            root.removeComponent<components::WorldSectorComponent>();
        }

        // VK-1594: free the GPU geometry too. In-memory HLOD meshes are pinned in
        // MeshStreamManager precisely so the ordinary eviction sweep cannot drop them, which also
        // means closing the world without this would leak every resident proxy's buffers.
        {
            std::vector<std::string> hlodMeshKeys;
            hlodProxyManager.collectMeshKeys(hlodMeshKeys);
            hlodProxyManager.unloadAll(*sceneGraph);

            for (const auto& meshKey : hlodMeshKeys)
            {
                events::render::objectstreaming::ReleaseHLODMeshCommand relCmd;
                relCmd.meshKey = meshKey;
                try
                {
                    ::events::EventDispatcher::instance().execute(relCmd);
                }
                catch (const std::exception&)
                {
                    // execute() throws with no handler registered, which is the normal state
                    // outside the Editor (ObjectStreamingServiceImpl is Editor-only). Nothing was
                    // registered there in the first place.
                }
            }
        }
        hlodStreamer.clear();
        hlodRegenQueue.clear();

        worldDefinition = {};
        // VK-1599: back to the one default grid the emptied definition describes, so nothing
        // outlives the world it belonged to.
        rebuildGridRuntimes();
        resetStreamingSessionState(); // VK-1595: the override and the freeze die with the world
        for (auto& grid : grids)
            grid->streamer.setEnabled(false);
        worldMode = false;
        currentWorldPath.clear();
    }

} // namespace services
