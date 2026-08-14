#include "WorldSectorServiceImpl.hpp"
#include "scene/SceneGraphSystem.hpp"
#include "scene/Entity.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "world/WorldSectorSerialization.hpp"
#include "world/WorldDefinitionSerialization.hpp"
#include "world/SectorRepartitionPlanner.hpp"
#include "resource/AtomicFileReplace.hpp"
#include "asset/AssetMetadataSerializer.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/world/WorldSectorEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../common/ProjectPaths.hpp"
#include "print/Log.hpp"
#include <cmath>
#include <filesystem>
#include <system_error>
#include <unordered_set>
#include <utility>

// ============================================================================
// VK-1598 - world re-partition and cell-size migration.
//
// The migration is COLD: it never spawns an entity. Each source .vfsector is read back to its
// entity JSON, each node's bucketing traits are read off that JSON, and the node is MOVED verbatim
// into the target sector's array. Nothing is deserialized, so UUIDs, components and nested children
// cannot be lost or re-minted - the ticket's hardest acceptance criterion becomes structurally
// impossible to violate rather than something to test for. VK-1597 shaped EntityStreamingTraits as
// a registry-free POD for exactly this caller (SectorAssignment.hpp).
//
// The price is that the op reads the SAVED world, so it runs Save World first and refuses if that
// fails.
// ============================================================================

namespace services
{
    namespace
    {
        namespace fs = std::filesystem;

        // VK-1599: the naming convention now lives in world::sectorFileName, shared with saveWorld
        // - the two copies had to agree and nothing enforced it.

        // The tier-0 bake that loadWorld probes for beside each sector file.
        fs::path tier0HlodPathFor(const fs::path& sectorPath)
        {
            fs::path hlod = sectorPath;
            hlod.replace_extension();
            hlod += "_hlod0.vfHLOD";
            return hlod;
        }

        // Rename that treats "the source does not exist" as success - a sector listed in the
        // .vfworld whose file is already gone is not a reason to abort a backup.
        bool moveIfPresent(const fs::path& from, const fs::path& to)
        {
            std::error_code ec;
            if (!fs::exists(from, ec))
                return true;

            fs::rename(from, to, ec);
            if (ec)
            {
                vfLogError("[Repartition] Failed to move '{}' -> '{}': {}",
                           from.string(), to.string(), ec.message());
                return false;
            }
            return true;
        }
    }

    // ---- guards -------------------------------------------------------------------------

    std::string WorldSectorServiceImpl::repartitionRefusal(uint8_t gridIndex,
                                                           const world::SectorConfig* newConfig) const
    {
        if (!worldMode)
            return "No world is open.";

        // Same reason MarkEntitySectorDirtyCommand and the Data Layers tab are edit-mode only:
        // play mode owns a transient entity population, and play->edit runs sectorManager.clear().
        if (isPlayMode)
            return "Stop play mode before repartitioning.";

        if (currentWorldPath.empty())
            return "The world has no file path - save it first.";

        // The bake holds an immutable snapshot of the sector file list and its workers are reading
        // those files right now; the same argument saveWorld already makes.
        if (hlodBaker.isRunning())
            return "An HLOD bake is in progress - cancel it first.";

        // Both trackers, not just sectors. An in-flight .vfHLOD read holds the file open on a
        // scheduler worker, and every tier-0 bake is moved into the backup by name - MoveFileEx
        // refuses to rename a file an ifstream still has open (MSVC opens _SH_DENYNO, which does
        // not include share-delete). That would surface as a rollback rather than corruption, but
        // it is a failure the user cannot act on, so refuse up front instead.
        // VK-1599: every grid, not just the target one. The swap renames files inside the shared
        // sectors/ directory, and a read in flight on ANY grid holds a handle in there.
        bool anyRequests = false;
        for (const auto& grid : grids)
            anyRequests = anyRequests || !grid->sectorRequests.empty();
        if (anyRequests || !hlodRequests.empty())
            return "Sector or HLOD reads are still in flight - wait for streaming to settle.";

        bool inFlight = false;
        for (const auto& grid : grids)
        {
            grid->manager.forEachSector([&](const world::WorldSector& sector)
            {
                if (sector.state == world::SectorState::Loading ||
                    sector.state == world::SectorState::Unloading ||
                    sector.state == world::SectorState::Prefetching)
                    inFlight = true;
            });
        }
        if (inFlight)
            return "Sectors are still loading or unloading - wait for streaming to settle.";

        // saveWorld hard-codes <worldDir>/sectors and names files sector_<x>_<z>.vfsector with no
        // world name in them, so two .vfworld files in one folder already share a sector directory
        // AND collide on filenames. Backing up and replacing that set would take the other world's
        // sectors with it. Pre-existing hazard; refusing is the only honest thing to do here.
        {
            std::error_code ec;
            const fs::path worldDir = fs::path(currentWorldPath).parent_path();
            const fs::path thisWorld = fs::path(currentWorldPath).filename();

            // A construction failure leaves the iterator at end(), so the loop simply does not run
            // and the guard passes - the same outcome as a folder with no sibling world.
            for (const auto& entry : fs::directory_iterator(worldDir, ec))
            {
                std::error_code statEc;
                if (!entry.is_regular_file(statEc) || statEc)
                    continue;
                if (entry.path().extension() != ".vfworld")
                    continue;
                if (entry.path().filename() == thisWorld)
                    continue;

                return "'" + entry.path().filename().string() + "' shares this folder, and both "
                       "worlds share the 'sectors' directory. Move one of them out first.";
            }
        }

        if (!newConfig)
            return {}; // convert-to-flat has no target config to validate

        // sectorWorldSize is the divisor in worldPositionToSectorCoord; a non-positive value
        // collapses every entity into sector 0. Same rule the creation wizard enforces.
        if (!std::isfinite(newConfig->sectorWorldSize) || newConfig->sectorWorldSize <= 0.0f)
            return "Sector size must be a finite value greater than 0.";

        if (newConfig->tilesPerSector < 1)
            return "Tiles per sector must be at least 1.";

        if (newConfig->alignedToTerrain)
        {
            float worldTileSize = 0.0f;
            try
            {
                worldTileSize = ::events::EventDispatcher::instance().query(
                    ::events::terrain::GetActiveTerrainTileSizeQuery{});
            }
            catch (const std::exception&)
            {
                // query() throws with no handler registered, which is the normal state outside the
                // Editor. With no terrain service there is nothing to align against, so the claim
                // is simply not checkable rather than false.
                worldTileSize = 0.0f;
            }

            if (worldTileSize > 0.0f && !world::isSectorAlignedToTerrain(*newConfig, worldTileSize))
            {
                return "Sector size " + std::to_string(newConfig->sectorWorldSize) +
                       " is not " + std::to_string(newConfig->tilesPerSector) +
                       " x the terrain tile size (" + std::to_string(worldTileSize) +
                       "). Uncheck \"Align to Terrain Grid\" or pick a matching size.";
            }
        }

        const world::SectorConfig& current = gridRuntime(gridIndex).manager.getConfig();
        if (current.sectorWorldSize == newConfig->sectorWorldSize &&
            current.tilesPerSector == newConfig->tilesPerSector &&
            current.alignedToTerrain == newConfig->alignedToTerrain)
        {
            return "The world already uses this sector configuration.";
        }

        return {};
    }

    // ---- reading the source set ---------------------------------------------------------

    bool WorldSectorServiceImpl::readAllSourceSectors(
        uint8_t gridIndex, std::vector<world::RepartitionSourceSector>& out) const
    {
        const auto& gridPaths = worldDefinition.grid(gridIndex).sectorFilePaths;
        out.clear();
        out.reserve(gridPaths.size());

        for (const auto& [coord, storedPath] : gridPaths)
        {
            const std::string sectorPath = resolveProjectPath(storedPath);

            world::RepartitionSourceSector source;
            source.coord = coord;

            if (!world::WorldSectorSerialization::loadSector(sectorPath, source.entities,
                                                            &source.dataLayers))
            {
                // Abort rather than skip. A sector we cannot read is a sector whose entities would
                // silently vanish from the repartitioned world.
                vfLogError("[Repartition] Cannot read sector ({},{}) from '{}' - aborting.",
                           coord.x, coord.z, sectorPath);
                out.clear();
                return false;
            }

            std::error_code ec;
            const auto size = fs::file_size(sectorPath, ec);
            source.fileBytes = ec ? 0u : size;

            out.push_back(std::move(source));
        }

        return true;
    }

    // ---- dry run ------------------------------------------------------------------------

    world::RepartitionSummary WorldSectorServiceImpl::previewRepartition(
        uint8_t gridIndex, const world::SectorConfig& newConfig)
    {
        world::RepartitionSummary summary;

        summary.refusal = repartitionRefusal(gridIndex, &newConfig);
        if (!summary.refusal.empty())
            return summary; // valid stays false

        std::vector<world::RepartitionSourceSector> sources;
        if (!readAllSourceSectors(gridIndex, sources))
        {
            summary.refusal = "One or more .vfsector files could not be read (see the log).";
            return summary;
        }

        // Builds and discards the whole plan. Deliberately the same call Apply makes, so the
        // numbers shown are the numbers written - an estimator computed a second way would
        // eventually disagree with the thing it is estimating.
        world::RepartitionPlan plan =
            world::planRepartition(sources, gridRuntime(gridIndex).manager.getConfig(), newConfig);
        return plan.summary;
    }

    // ---- backup -------------------------------------------------------------------------

    bool WorldSectorServiceImpl::moveWorldFilesToBackup(
        const std::unordered_map<world::SectorCoord, std::string, world::SectorCoordHash>& paths,
        const std::unordered_map<world::HLODCellCoord, std::string,
                                 world::HLODCellCoordHash>& cells,
        const fs::path& backupDir) const
    {
        std::error_code ec;
        fs::create_directories(backupDir, ec);
        if (ec)
        {
            vfLogError("[Repartition] Cannot create backup directory '{}': {}",
                       backupDir.string(), ec.message());
            return false;
        }

        // Remembered so a failure part-way through can put everything back. Until the staged set
        // is moved into place this is fully reversible, and that is the whole reason the backup
        // happens before the commit rather than after it.
        std::vector<std::pair<fs::path, fs::path>> moved; // from -> to

        auto tryMove = [&](const fs::path& from) -> bool
        {
            std::error_code existsEc;
            if (!fs::exists(from, existsEc))
                return true;

            const fs::path to = backupDir / from.filename();
            if (!moveIfPresent(from, to))
                return false;

            moved.emplace_back(from, to);
            return true;
        };

        auto rollback = [&]()
        {
            for (auto it = moved.rbegin(); it != moved.rend(); ++it)
            {
                std::error_code restoreEc;
                fs::rename(it->second, it->first, restoreEc);
                if (restoreEc)
                {
                    vfLogError("[Repartition] Rollback failed for '{}' - the file is still in '{}'.",
                               it->first.string(), backupDir.string());
                }
            }
        };

        for (const auto& [coord, storedPath] : paths)
        {
            const fs::path sectorPath = resolveProjectPath(storedPath);
            if (!tryMove(sectorPath) || !tryMove(tier0HlodPathFor(sectorPath)))
            {
                rollback();
                return false;
            }
        }

        // Tier 1+ bakes sit next to the .vfworld rather than in sectors/, so they need naming
        // explicitly (HLODCellPlanner::hlodOutputPathForCell).
        for (const auto& [cell, storedPath] : cells)
        {
            if (storedPath.empty())
                continue;

            if (!tryMove(resolveProjectPath(storedPath)))
            {
                rollback();
                return false;
            }
        }

        return true;
    }

    // ---- teardown -----------------------------------------------------------------------

    void WorldSectorServiceImpl::destroyAllSectorEntities()
    {
        // VK-1599: (grid, coord, uuids). Every grid - convert-to-flat and the reload after a
        // repartition both need the WHOLE world torn down, not just the primary grid.
        struct PendingUnload
        {
            uint8_t gridIndex;
            world::SectorCoord coord;
            std::vector<uint64_t> uuids;
        };
        std::vector<PendingUnload> toUnload;

        for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
        {
        gridRuntime(gridIndex).manager.forEachSector([&](world::WorldSector& sector)
        {
            if (sector.entityUUIDs.empty())
                return;

            // entityUUIDs is a parse-time root list that can legitimately hold a UUID more than
            // once (a .vfsector written by the pre-VK-1598 save path lists every entity twice), and
            // queueing the same entity twice would try to destroy an already-dead handle.
            std::vector<uint64_t> uuids;
            std::unordered_set<uint64_t> seen;
            uuids.reserve(sector.entityUUIDs.size());
            for (uint64_t uuid : sector.entityUUIDs)
            {
                if (seen.insert(uuid).second)
                    uuids.push_back(uuid);
            }

            toUnload.push_back({gridIndex, sector.coord, std::move(uuids)});
        });
        }

        // Goes through the loader rather than calling sceneGraph.removeEntity directly, so the
        // established pre-destroy path still runs (physics/animation/VFX/audio snapshots, reference
        // resolver bookkeeping). clearWorld drops those snapshots straight afterwards.
        for (auto& pending : toUnload)
            entityLoader.queueSectorUnload(pending.gridIndex, pending.coord, pending.uuids);

        entityLoader.flush(*sceneGraph);
    }

    // ---- apply --------------------------------------------------------------------------

    bool WorldSectorServiceImpl::applyRepartition(uint8_t gridIndex,
                                                  const world::SectorConfig& newConfig,
                                                  world::RepartitionSummary* outSummary)
    {
        gridIndex = worldDefinition.clampGridIndex(gridIndex);
        const std::string refusal = repartitionRefusal(gridIndex, &newConfig);
        if (!refusal.empty())
        {
            vfLogError("[Repartition] Refused: {}", refusal);
            if (outSummary)
                outSummary->refusal = refusal;
            return false;
        }

        // The migration reads the SAVED world, so make the saved world current before reading it.
        // Failing here is the safe failure: nothing has been touched.
        //
        // Empty path, not currentWorldPath: saveWorld reassigns that member, so passing it would
        // hand the function a reference into the very string it overwrites.
        if (!saveWorld({}))
        {
            vfLogError("[Repartition] Save World failed - refusing to migrate an unsaved world.");
            if (outSummary)
                outSummary->refusal = "Save World failed (see the log).";
            return false;
        }

        const std::string worldPath = currentWorldPath;
        const fs::path worldDir = fs::path(worldPath).parent_path();
        const fs::path sectorsDir = worldDir / "sectors";
        const fs::path stagingDir = worldDir / "sectors.new";
        const fs::path backupDir = worldDir / "sectors.bak";
        const fs::path tmpWorldPath = fs::path(worldPath + ".tmp");
        const fs::path bakWorldPath = fs::path(worldPath + ".bak");

        std::vector<world::RepartitionSourceSector> sources;
        if (!readAllSourceSectors(gridIndex, sources))
        {
            if (outSummary)
                outSummary->refusal = "One or more .vfsector files could not be read (see the log).";
            return false;
        }

        const world::SectorConfig oldConfig = gridRuntime(gridIndex).manager.getConfig();
        world::RepartitionPlan plan = world::planRepartition(sources, oldConfig, newConfig);
        if (outSummary)
            *outSummary = plan.summary;

        if (!plan.summary.valid)
        {
            vfLogError("[Repartition] Refused: {}", plan.summary.refusal);
            return false;
        }

        // ---- stage: write the complete new set somewhere the live world cannot see ----
        std::error_code ec;
        fs::remove_all(stagingDir, ec); // a leftover from an interrupted run
        fs::create_directories(stagingDir, ec);
        if (ec)
        {
            vfLogError("[Repartition] Cannot create staging directory '{}': {}",
                       stagingDir.string(), ec.message());
            return false;
        }

        auto abandonStaging = [&]()
        {
            std::error_code cleanupEc;
            fs::remove_all(stagingDir, cleanupEc);
            fs::remove(tmpWorldPath, cleanupEc);
        };

        for (const auto& target : plan.targets)
        {
            const fs::path stagedPath = stagingDir / world::sectorFileName(gridIndex, target.coord);
            if (!world::WorldSectorSerialization::saveSectorFromEntityData(
                    target.coord, target.entities, target.dataLayers, stagedPath.string()))
            {
                vfLogError("[Repartition] Failed to write staged sector ({},{}) - aborting, "
                           "nothing has been replaced.", target.coord.x, target.coord.z);
                abandonStaging();
                return false;
            }

            // Past the OS write cache, so what survives a power loss is a complete file rather
            // than a plausible-looking prefix (VK-1644).
            resource::flushFileToDisk(stagedPath);
        }

        // ---- stage: the .vfworld that names them ----
        world::WorldDefinition newDefinition = worldDefinition;
        newDefinition.grid(gridIndex).sectorConfig = newConfig;
        newDefinition.grid(gridIndex).sectorFilePaths.clear();
        for (const auto& target : plan.targets)
        {
            const fs::path finalPath = sectorsDir / world::sectorFileName(gridIndex, target.coord);
            newDefinition.grid(gridIndex).sectorFilePaths[target.coord] = toProjectRelativePath(finalPath.string());
        }
        // Every bake was built from the OLD partition, so all tiers are stale by definition. The
        // files themselves go into the backup below, which is why invalidateHLODForSector is not
        // called here - it keys on coords that are about to stop existing.
        newDefinition.hlodCells.clear();

        if (!world::WorldDefinitionSerialization::save(newDefinition, tmpWorldPath.string()))
        {
            vfLogError("[Repartition] Failed to write '{}' - aborting.", tmpWorldPath.string());
            abandonStaging();
            return false;
        }
        resource::flushFileToDisk(tmpWorldPath);

        // ---- commit ----
        // Everything above is reversible; from here on the world on disk changes.
        // The previous generation's backup goes first - keeping two would double the disk cost of
        // every migration, and it describes a state the user has already migrated away from.
        fs::remove_all(backupDir, ec);
        fs::remove(bakWorldPath, ec);

        // Copied BEFORE the sectors move, so the backup pair is complete as early as possible: a
        // .vfworld.bak with no sectors.bak beside it is recoverable, the reverse is not. The .vfmeta
        // sidecar is deliberately untouched - its GUID identifies this asset and must survive.
        fs::copy_file(worldPath, bakWorldPath, fs::copy_options::overwrite_existing, ec);
        if (ec)
            vfLogWarning("[Repartition] Could not write '{}': {}", bakWorldPath.string(), ec.message());

        // VK-1599: only the grid being repartitioned gives up its files - the other grids' sectors
        // stay exactly where they are, so the outgoing set is this grid's inventory alone. HLOD is
        // invalidated wholesale either way: its cells are keyed on primary-grid coords that a
        // repartition of the primary grid stops meaning anything.
        if (!moveWorldFilesToBackup(worldDefinition.grid(gridIndex).sectorFilePaths,
                                    worldDefinition.hlodCells, backupDir))
        {
            // moveWorldFilesToBackup rolled itself back, so the live world is intact.
            vfLogError("[Repartition] Could not back up the outgoing sector set - aborting.");
            abandonStaging();
            if (outSummary)
                outSummary->refusal = "Could not back up the outgoing sector set (see the log).";
            return false;
        }

        fs::create_directories(sectorsDir, ec);
        for (const auto& target : plan.targets)
        {
            const std::string name = world::sectorFileName(gridIndex, target.coord);
            if (!moveIfPresent(stagingDir / name, sectorsDir / name))
            {
                // Past the point of automatic recovery: the outgoing set is in sectors.bak and the
                // incoming set is split between two directories. Say exactly where everything is
                // rather than pretending this is retryable.
                vfLogError("[Repartition] COMMIT FAILED while moving the new sector set into place. "
                           "The previous world is intact in '{}' and '{}'; the new set is in '{}'. "
                           "Restore by hand.",
                           backupDir.string(), worldPath, stagingDir.string());
                return false;
            }
        }
        fs::remove_all(stagingDir, ec);

        if (!resource::replaceFileAtomically(tmpWorldPath, worldPath))
        {
            vfLogError("[Repartition] COMMIT FAILED while replacing '{}'. The new sector set is in "
                       "place but the world file still describes the old partition; the previous "
                       "sectors are in '{}'.", worldPath, backupDir.string());
            return false;
        }

        // ---- reconcile the live world with what is now on disk ----
        // clearWorld leaves streamed entities alive in the scene graph, and SectorEntityLoader
        // skips any UUID already in the registry - so without this sweep the reloaded sectors come
        // back empty and every entity is stranded at its old coord.
        destroyAllSectorEntities();
        clearWorld();

        if (!loadWorld(worldPath))
        {
            vfLogError("[Repartition] The new world was written but could not be reloaded: '{}'.",
                       worldPath);
            return false;
        }

        // clearWorld dropped the root's world tag and loadWorld does not re-add it (only
        // createWorld and saveWorld do), so restore it here or the scene stops auto-loading its
        // world until the next Save World.
        sceneGraph->GetRoot().addOrReplaceComponent<components::WorldSectorComponent>().worldFilePath =
            toProjectRelativePath(worldPath);

        vfLogInfo("[Repartition] {} -> {} world units: {} sector(s) -> {} sector(s), {} entities, "
                  "{} moved, {} duplicate(s) dropped, {} data-layer copies.",
                  oldConfig.sectorWorldSize, newConfig.sectorWorldSize,
                  plan.summary.sourceSectorCount, plan.summary.targetSectorCount,
                  plan.summary.entityCount, plan.summary.movedCount,
                  plan.summary.duplicatesDropped, plan.summary.layerCopies);

        if (plan.summary.layerNameCollisions > 0)
        {
            vfLogWarning("[Repartition] {} data-layer name collision(s): two source sectors "
                         "contributed the same layer name to one target and blobs cannot be "
                         "merged. The first source in (z,x) order won - review them in the Data "
                         "Layers tab.", plan.summary.layerNameCollisions);
        }

        ::events::world::WorldRepartitionedNotification notif;
        notif.sectorConfig = newConfig;
        notif.summary = plan.summary;
        notif.backupDirectory = backupDir.string();
        ::events::EventDispatcher::instance().publish(notif);

        return true;
    }

    // ---- convert to flat ----------------------------------------------------------------

    bool WorldSectorServiceImpl::convertWorldToFlat()
    {
        // Convert-to-flat closes the world outright, so its guards are not grid-specific; the
        // primary grid stands in for "any grid" in the in-flight checks, which loop them all.
        const std::string refusal = repartitionRefusal(world::kPrimaryGridIndex, nullptr);
        if (!refusal.empty())
        {
            vfLogError("[Flatten] Refused: {}", refusal);
            return false;
        }

        if (!saveWorld({})) // see applyRepartition - saveWorld reassigns currentWorldPath
        {
            vfLogError("[Flatten] Save World failed - refusing to flatten an unsaved world.");
            return false;
        }

        const std::string worldPath = currentWorldPath;
        const fs::path worldDir = fs::path(worldPath).parent_path();
        const fs::path backupDir = worldDir / "sectors.bak";

        // Spawn everything, synchronously. Deliberately NOT the streaming path: loadSector is
        // async and drainSectorLoads CANCELS rather than waits, so it is the wrong tool for
        // "bring the whole world in". queueSectorLoadFromData + flush is the same spawn code the
        // streamer ends up in, minus the budget.
        //
        // VK-1599: EVERY grid. Flattening dissolves the world into a plain scene, so an entity left
        // in a clutter grid's .vfsector would simply be lost. The dedupe set spans grids too - a
        // UUID can only exist once in the registry however many grids list it.
        std::unordered_set<uint64_t> uniqueUUIDs;
        for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
        {
            std::vector<world::RepartitionSourceSector> sources;
            if (!readAllSourceSectors(gridIndex, sources))
                return false;

            for (auto& source : sources)
            {
                std::vector<std::pair<std::string, nlohmann::json>> payload;
                payload.reserve(source.entities.size());

                for (auto& entityJson : source.entities)
                {
                    if (auto it = entityJson.find("uuid");
                        it != entityJson.end() && it->is_number_unsigned())
                    {
                        // A duplicate would be skipped by the loader's own registry check anyway;
                        // dropping it here keeps the reported count honest.
                        if (!uniqueUUIDs.insert(it->get<uint64_t>()).second)
                            continue;
                    }

                    std::string name = entityJson.value("name", "Unnamed");
                    payload.emplace_back(std::move(name), std::move(entityJson));
                }

                source.entities.clear();
                entityLoader.queueSectorLoadFromData(gridIndex, source.coord, payload);
            }
        }

        entityLoader.flush(*sceneGraph);

        // Back the world up before removing it. Per-sector data layers are a sector concept and do
        // not survive flattening - the backup is the only copy, which is exactly why the move
        // happens rather than a delete.
        //
        // One backup generation, same policy as a repartition: a leftover sectors.bak from an
        // earlier migration would otherwise interleave with this one and neither would be a usable
        // snapshot of anything.
        std::error_code ec;
        fs::remove_all(backupDir, ec);

        // VK-1599: flattening takes EVERY grid's sectors - the world is being dissolved into a
        // plain scene, so nothing may be left behind. HLOD cells go with the first pass.
        for (uint8_t gridIndex = 0; gridIndex < gridCount(); ++gridIndex)
        {
            const bool first = (gridIndex == 0);
            if (!moveWorldFilesToBackup(worldDefinition.grid(gridIndex).sectorFilePaths,
                                        first ? worldDefinition.hlodCells
                                              : std::unordered_map<world::HLODCellCoord, std::string,
                                                                   world::HLODCellCoordHash>{},
                                        backupDir))
            {
                vfLogError("[Flatten] Could not back up the sector set - aborting. The entities are "
                           "now resident in the scene; reload the scene to discard them.");
                return false;
            }
        }

        // The .vfmeta goes with it here (unlike a repartition, which keeps the world file and so
        // must keep its GUID): a flattened project has no .vfworld for the sidecar to describe.
        const fs::path metaPath = asset::AssetMetadataSerializer::getMetaPath(fs::path(worldPath));
        moveIfPresent(worldPath, backupDir / fs::path(worldPath).filename());
        moveIfPresent(metaPath, backupDir / metaPath.filename());

        const auto entityCount = static_cast<uint32_t>(uniqueUUIDs.size());

        // Drops WorldSectorComponent from the root and turns GPU object streaming off, so the
        // entities render through the ordinary scene path from here on. It deliberately does NOT
        // destroy them - that is what makes this the flat world.
        clearWorld();

        vfLogInfo("[Flatten] {} entities are now plain scene entities. The world files were moved "
                  "to '{}'. The scene is UNSAVED - use File > Save Scene to persist them.",
                  entityCount, backupDir.string());

        ::events::world::WorldFlattenedNotification notif;
        notif.entityCount = entityCount;
        notif.backupDirectory = backupDir.string();
        ::events::EventDispatcher::instance().publish(notif);

        return true;
    }

} // namespace services
