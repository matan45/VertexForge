#include "NavmeshServiceImpl.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/terrain/BrushEvents.hpp"
#include "../../events/terrain/HoleBrushEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/render/DebugDrawEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "navigation/NavmeshSerializer.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"
#include <asset/AssetRef.hpp>
#include "threading/JobSystem.hpp"
#include <cassert>

namespace services
{
    NavmeshServiceImpl::NavmeshServiceImpl(INavmeshProvider* navmeshProvider)
        : navmeshProvider(navmeshProvider),
          tileManager(navmeshProvider, lastBakeSettings,
                      [this](const navigation::NavmeshTileBounds& bounds,
                             const types::NavmeshBakeSettings& settings,
                             navigation::NavmeshInputGeometry& outGeometry)
                      {
                          collectTileGeometry(bounds, settings, outGeometry);
                      }),
          agentManager(navmeshProvider,
                       [this](const glm::vec3& pos)
                       {
                           return tileManager.worldToTileCoord(pos);
                       })
    {
        assert(navmeshProvider && "NavmeshProvider must not be null");
        tileManager.setOffMeshLinkCollector(
            [this](const navigation::NavmeshTileBounds& bounds,
                   const types::NavmeshBakeSettings& settings)
            {
                return collectOffMeshLinksForTile(bounds, settings);
            });
        tileManager.setAreaModifierCollector(
            [this](const navigation::NavmeshTileBounds& bounds)
            {
                return collectAreaModifiersForTile(bounds);
            });

        NavmeshWorldBaker::WorldOps worldOps;
        worldOps.getAllSectorCoords = []()
        {
            return ::events::EventDispatcher::instance().query(::events::world::GetAllSectorCoordsQuery{});
        };
        worldOps.loadSector = [](const ::world::SectorCoord& coord)
        {
            ::events::world::LoadSectorCommand cmd;
            cmd.coord = coord;
            return ::events::EventDispatcher::instance().execute(cmd);
        };
        worldOps.unloadSector = [](const ::world::SectorCoord& coord)
        {
            ::events::world::UnloadSectorCommand cmd;
            cmd.coord = coord;
            return ::events::EventDispatcher::instance().execute(cmd);
        };
        worldOps.sectorExists = [](const ::world::SectorCoord& coord)
        {
            ::events::world::DoesSectorExistQuery query;
            query.coord = coord;
            return ::events::EventDispatcher::instance().query(query);
        };
        worldOps.getReadiness = [](const ::world::SectorCoord& coord)
        {
            ::events::world::GetSectorReadinessQuery query;
            query.coord = coord;
            return ::events::EventDispatcher::instance().query(query);
        };
        worldOps.getSectorWorldSize = []()
        {
            return ::events::EventDispatcher::instance()
                .query(::events::world::GetSectorConfigQuery{}).sectorWorldSize;
        };
        worldOps.registerKeepAliveSource = [](const glm::vec3& position)
        {
            ::events::world::RegisterStreamingSourceCommand cmd;
            cmd.position = position;
            cmd.radiusMultiplier = 1.0f;
            cmd.priority = 255;
            return ::events::EventDispatcher::instance().execute(cmd);
        };
        worldOps.updateKeepAliveSource = [](uint32_t sourceId, const glm::vec3& position)
        {
            ::events::world::UpdateStreamingSourcePositionCommand cmd;
            cmd.sourceId = sourceId;
            cmd.position = position;
            ::events::EventDispatcher::instance().execute(cmd);
        };
        worldOps.unregisterKeepAliveSource = [](uint32_t sourceId)
        {
            ::events::world::UnregisterStreamingSourceCommand cmd;
            cmd.sourceId = sourceId;
            ::events::EventDispatcher::instance().execute(cmd);
        };
        worldBaker = std::make_unique<NavmeshWorldBaker>(tileManager, std::move(worldOps));
        worldBaker->setCompletionCallback([this](bool success, const std::string& message)
        {
            if (success && tileManager.getTileCache())
                attachNavmeshAssetToSceneRoot(tileManager.getTileCache()->getDirectory() + "/index.vfNavIndex");

            ::events::navmesh::WorldNavmeshBakeCompleteNotification notif;
            notif.success = success;
            notif.message = message;
            ::events::EventDispatcher::instance().publish(notif);
        });
    }

    NavmeshServiceImpl::~NavmeshServiceImpl()
    {
        if (bakeFuture.valid())
        {
            bakeFuture.wait();
        }
        tileManager.unregisterEvents();
    }

    void NavmeshServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<events::navmesh::BakeNavmeshCommand>(
            [this](const events::navmesh::BakeNavmeshCommand& cmd)
            {
                bakeNavmesh(cmd.settings);
            });

        dispatcher.registerCommandHandler<events::navmesh::SaveNavmeshCommand>(
            [this](const events::navmesh::SaveNavmeshCommand& cmd)
            {
                return saveNavmesh(cmd.filePath);
            });

        dispatcher.registerCommandHandler<events::navmesh::LoadNavmeshCommand>(
            [this](const events::navmesh::LoadNavmeshCommand& cmd)
            {
                return loadNavmesh(cmd.filePath);
            });

        dispatcher.registerCommandHandler<events::navmesh::ClearNavmeshCommand>(
            [this](const events::navmesh::ClearNavmeshCommand&)
            {
                clearNavmesh();
            });

        dispatcher.registerCommandHandler<events::navmesh::AddAgentCommand>(
            [this](const events::navmesh::AddAgentCommand& cmd)
            {
                addAgent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::navmesh::RemoveAgentCommand>(
            [this](const events::navmesh::RemoveAgentCommand& cmd)
            {
                removeAgent(cmd.entity);
            });

        dispatcher.registerCommandHandler<events::navmesh::SetAgentDestinationCommand>(
            [this](const events::navmesh::SetAgentDestinationCommand& cmd)
            {
                setAgentDestination(cmd.entity, cmd.target);
            });

        dispatcher.registerCommandHandler<events::navmesh::StopAgentCommand>(
            [this](const events::navmesh::StopAgentCommand& cmd)
            {
                stopAgent(cmd.entity);
            });


        dispatcher.registerQueryHandler<events::navmesh::FindPathQuery>(
            [this](const events::navmesh::FindPathQuery& query)
            {
                return findPath(query.start, query.end, query.agentRadius, query.agentHeight);
            });

        dispatcher.registerQueryHandler<events::navmesh::GetClosestPointQuery>(
            [this](const events::navmesh::GetClosestPointQuery& query)
            {
                return getClosestPointOnNavmesh(query.point, query.searchRadius);
            });

        dispatcher.registerQueryHandler<events::navmesh::IsPointOnNavmeshQuery>(
            [this](const events::navmesh::IsPointOnNavmeshQuery& query)
            {
                return isPointOnNavmesh(query.point, query.tolerance);
            });

        dispatcher.registerQueryHandler<events::navmesh::NavmeshRaycastQuery>(
            [this](const events::navmesh::NavmeshRaycastQuery& query)
            {
                return navmeshProvider->navmeshRaycast(query.from, query.to);
            });

        dispatcher.registerCommandHandler<events::navmesh::UpdateAgentConfigCommand>(
            [this](const events::navmesh::UpdateAgentConfigCommand& cmd)
            {
                agentManager.updateAgentConfig(cmd.entity, cmd.maxSpeed, cmd.maxAcceleration, cmd.rootMotionDriven, cmd.rootMotionSpeedScale);
            });

        dispatcher.registerQueryHandler<events::navmesh::GetAgentVelocityQuery>(
            [this](const events::navmesh::GetAgentVelocityQuery& query)
            {
                return agentManager.getAgentVelocity(query.entity);
            });

        dispatcher.registerQueryHandler<events::navmesh::GetAgentSpeedQuery>(
            [this](const events::navmesh::GetAgentSpeedQuery& query)
            {
                return agentManager.getAgentSpeed(query.entity);
            });

        dispatcher.registerQueryHandler<events::navmesh::HasNavmeshQuery>(
            [this](const events::navmesh::HasNavmeshQuery&)
            {
                return hasNavmesh();
            });

        dispatcher.registerQueryHandler<events::navmesh::GetBakeProgressQuery>(
            [this](const events::navmesh::GetBakeProgressQuery&)
            {
                return getBakeProgress();
            });

        dispatcher.registerQueryHandler<events::navmesh::GetNavmeshSettingsQuery>(
            [this](const events::navmesh::GetNavmeshSettingsQuery&)
            {
                return lastBakeSettings;
            });

        dispatcher.registerQueryHandler<events::navmesh::GetNavmeshDebugMeshQuery>(
            [this](const events::navmesh::GetNavmeshDebugMeshQuery&)
            {
                events::navmesh::NavmeshDebugMeshResult result;
                getNavmeshDebugMesh(result.vertices, result.indices);
                return result;
            });

        // === Per-Tile Events (VK-739) ===

        dispatcher.registerCommandHandler<events::navmesh::BakeTileCommand>(
            [this](const events::navmesh::BakeTileCommand& cmd)
            {
                return bakeSingleTile(cmd.tileX, cmd.tileZ);
            });

        dispatcher.registerCommandHandler<events::navmesh::BakeAllTilesCommand>(
            [this](const events::navmesh::BakeAllTilesCommand& cmd)
            {
                bakeNavmesh(cmd.settings);
            });

        dispatcher.registerCommandHandler<events::navmesh::SaveNavmeshTiledCommand>(
            [this](const events::navmesh::SaveNavmeshTiledCommand& cmd)
            {
                return saveNavmeshTiled(cmd.directory);
            });

        dispatcher.registerCommandHandler<events::navmesh::LoadNavmeshTiledCommand>(
            [this](const events::navmesh::LoadNavmeshTiledCommand& cmd)
            {
                return loadNavmeshTiled(cmd.directory);
            });

        dispatcher.registerCommandHandler<events::navmesh::SetNavmeshStreamingConfigCommand>(
            [this](const events::navmesh::SetNavmeshStreamingConfigCommand& cmd)
            {
                tileManager.getStreamer().setConfig(cmd.config);
            });

        dispatcher.registerCommandHandler<events::navmesh::SetNavmeshStreamingEnabledCommand>(
            [this](const events::navmesh::SetNavmeshStreamingEnabledCommand& cmd)
            {
                tileManager.getStreamer().setEnabled(cmd.enabled);
                // Streaming off = whole navmesh resident again (terrain parity)
                if (!cmd.enabled)
                    tileManager.loadAllTilesFromCache();
            });

        dispatcher.registerCommandHandler<events::navmesh::LoadAllNavmeshTilesCommand>(
            [this](const events::navmesh::LoadAllNavmeshTilesCommand&)
            {
                return tileManager.loadAllTilesFromCache() >= 0;
            });

        dispatcher.registerCommandHandler<events::navmesh::BakeWorldNavmeshCommand>(
            [this](const events::navmesh::BakeWorldNavmeshCommand& cmd) -> bool
            {
                if (playModeActive)
                {
                    vfLogWarning("NavmeshService: world bake is edit-mode only");
                    return false;
                }
                if (!::events::EventDispatcher::instance().query(::events::world::IsWorldModeQuery{}))
                {
                    vfLogWarning("NavmeshService: world bake requires world mode");
                    return false;
                }
                if (bakeFuture.valid() || worldBaker->isRunning())
                    return false;

                // Fresh tiled navmesh with the requested settings — tileManager
                // holds a reference to lastBakeSettings
                clearNavmesh();
                lastBakeSettings = cmd.settings;
                return worldBaker->start(cmd.outputDirectory);
            });

        dispatcher.registerCommandHandler<events::navmesh::CancelWorldNavmeshBakeCommand>(
            [this](const events::navmesh::CancelWorldNavmeshBakeCommand&)
            {
                worldBaker->cancel();
            });

        dispatcher.registerQueryHandler<events::navmesh::GetWorldNavmeshBakeProgressQuery>(
            [this](const events::navmesh::GetWorldNavmeshBakeProgressQuery&)
            {
                return worldBaker->getProgress();
            });

        dispatcher.registerQueryHandler<events::navmesh::GetNavmeshStreamingConfigQuery>(
            [this](const events::navmesh::GetNavmeshStreamingConfigQuery&)
            {
                return tileManager.getStreamer().getConfig();
            });

        dispatcher.registerQueryHandler<events::navmesh::IsNavmeshStreamingEnabledQuery>(
            [this](const events::navmesh::IsNavmeshStreamingEnabledQuery&)
            {
                return tileManager.getStreamer().isEnabled();
            });

        dispatcher.registerQueryHandler<events::navmesh::GetNavmeshTileStatusQuery>(
            [this](const events::navmesh::GetNavmeshTileStatusQuery&)
            {
                std::vector<events::navmesh::NavmeshTileStatusInfo> result;
                if (tileManager.getTileCache())
                {
                    tileManager.getTileCache()->forEachTile([&](const navigation::NavmeshTileCoord& coord)
                    {
                        events::navmesh::NavmeshTileStatusInfo info;
                        info.coord = coord;
                        if (tileManager.getDirtyTiles().count(coord))
                            info.status = events::navmesh::NavmeshTileStatus::Dirty;
                        else if (tileManager.getStreamer().isTileLoaded(coord))
                            info.status = events::navmesh::NavmeshTileStatus::Loaded;
                        else
                            info.status = events::navmesh::NavmeshTileStatus::Baked;
                        result.push_back(info);
                    });
                }
                return result;
            });

        // Camera position tracking for streaming
        dispatcher.subscribe<events::render::CameraPositionUpdatedNotification>(
            [this](const events::render::CameraPositionUpdatedNotification& notif)
            {
                tileManager.setLastCameraPos(notif.position);
            });

        // Tile graph updates for hierarchical pathfinding (+ version bump for
        // script-side stale-path detection)
        dispatcher.subscribe<events::navmesh::NavmeshTileLoadedNotification>(
            [this](const events::navmesh::NavmeshTileLoadedNotification& notif)
            {
                ++tileVersion;
                navmeshProvider->onTileAdded(notif.tileX, notif.tileZ);
            });

        dispatcher.subscribe<events::navmesh::NavmeshTileUnloadedNotification>(
            [this](const events::navmesh::NavmeshTileUnloadedNotification& notif)
            {
                ++tileVersion;
                navmeshProvider->onTileRemoved(notif.tileX, notif.tileZ);
            });

        dispatcher.subscribe<events::navmesh::NavmeshTileUpdatedNotification>(
            [this](const events::navmesh::NavmeshTileUpdatedNotification& notif)
            {
                ++tileVersion;
                navmeshProvider->onTileAdded(notif.tileX, notif.tileZ);
            });

        dispatcher.registerQueryHandler<events::navmesh::GetNavmeshTileVersionQuery>(
            [this](const events::navmesh::GetNavmeshTileVersionQuery&)
            {
                return tileVersion;
            });

        // Brush event subscriptions for incremental rebake
        tileManager.registerEvents();
    }


    void NavmeshServiceImpl::bakeNavmesh(const types::NavmeshBakeSettings& settings)
    {
        if (bakeFuture.valid())
            return;

        lastBakeSettings = settings;

        navigation::NavmeshInputGeometry geometry;
        collectSceneGeometry(settings, geometry);

        if (geometry.isEmpty())
        {
            vfLogWarning("NavmeshService: No geometry collected for navmesh baking");
            auto& dispatcher = ::events::EventDispatcher::instance();
            events::navmesh::NavmeshBakeCompleteNotification notification;
            notification.success = false;
            notification.message = "No geometry found in scene";
            dispatcher.publish(notification);
            return;
        }

        vfLogInfo("NavmeshService: Baking navmesh with {} vertices, {} triangles",
                  geometry.getVertexCount(), geometry.getTriangleCount());

        auto offMeshLinks = collectAllOffMeshLinks(settings);
        if (settings.autoGenerateDropLinks)
            autoGenerateDropLinks(settings, offMeshLinks);

        auto areaModifiers = collectAllAreaModifiers();

        bakeFuture = threading::JobSystem::instance().submit(
            [this, geom = std::move(geometry), settings, links = std::move(offMeshLinks), mods = std::move(areaModifiers)]()
            {
                return navmeshProvider->buildNavmesh(geom, settings, links, mods);
            }, threading::JobPriority::LOW);
    }

    types::NavmeshBakeProgress NavmeshServiceImpl::getBakeProgress() const
    {
        return navmeshProvider->getBuildProgress();
    }

    void NavmeshServiceImpl::pollBakeCompletion()
    {
        if (!bakeFuture.valid())
            return;

        if (bakeFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;

        bool success = bakeFuture.get();

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::navmesh::NavmeshBakeCompleteNotification notification;
        notification.success = success;
        notification.message = success ? "Navmesh bake complete" : "Navmesh bake failed";
        dispatcher.publish(notification);

        if (success)
            vfLogInfo("NavmeshService: Navmesh bake complete");
        else
            vfLogError("NavmeshService: Navmesh bake failed");
    }


    bool NavmeshServiceImpl::saveNavmesh(const std::string& filePath)
    {
        if (!navmeshProvider->hasNavmesh())
        {
            vfLogWarning("NavmeshService: No navmesh to save");
            return false;
        }

        auto tiles = navmeshProvider->serializeNavmesh();

        navigation::NavmeshFileHeader header;
        header.settings = lastBakeSettings;
        header.tileCount = static_cast<uint32_t>(tiles.size());

        bool result = navigation::NavmeshSerializer::save(filePath, header, tiles);
        if (result)
        {
            attachNavmeshAssetToSceneRoot(filePath);

            events::resource::AssetSavedNotification notif;
            notif.filePath = filePath;
            ::events::EventDispatcher::instance().publish(notif);
        }
        return result;
    }

    void NavmeshServiceImpl::attachNavmeshAssetToSceneRoot(const std::string& assetPath)
    {
        auto rootHandle = ::events::EventDispatcher::instance().query(::events::scene::GetRootEntityQuery{});
        if (!rootHandle.isValid())
        {
            vfLogWarning("NavmeshService: No scene root to attach navmesh asset '{}'", assetPath);
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto rootEntity = internal::fromHandle(rootHandle);
        registry.emplace_or_replace<components::NavmeshComponent>(rootEntity,
            components::NavmeshComponent{asset::AssetRef::fromPath(assetPath)});
    }

    bool NavmeshServiceImpl::loadNavmesh(const std::string& filePath)
    {
        navigation::NavmeshFileHeader header;
        std::vector<navigation::NavmeshTileData> tiles;

        if (!navigation::NavmeshSerializer::load(filePath, header, tiles))
        {
            return false;
        }

        lastBakeSettings = header.settings;
        bool success = navmeshProvider->deserializeNavmesh(header, tiles);

        if (success)
        {
            auto& dispatcher = ::events::EventDispatcher::instance();
            events::navmesh::NavmeshBakeCompleteNotification notification;
            notification.success = true;
            notification.message = "Navmesh loaded from file";
            dispatcher.publish(notification);
        }

        return success;
    }

    bool NavmeshServiceImpl::hasNavmesh() const
    {
        return navmeshProvider->hasNavmesh();
    }

    void NavmeshServiceImpl::clearNavmesh()
    {
        cleanupPhantomAgents();
        navmeshProvider->clearNavmesh();
        agentManager.clear();
        tileManager.clear();
        lastObstaclePositions.clear();
        lastModifierVolumePositions.clear();

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::NavmeshComponent>();
        for (auto entity : view)
        {
            registry.remove<components::NavmeshComponent>(entity);
        }

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::render::ClearNavmeshDebugMeshCommand clearCmd;
        dispatcher.execute(clearCmd);
    }


    navigation::NavPath NavmeshServiceImpl::findPath(const glm::vec3& start, const glm::vec3& end,
                                                      float agentRadius, float agentHeight)
    {
        if (!navmeshProvider->hasNavmesh())
        {
            return {};
        }
        return navmeshProvider->findPath(start, end, agentRadius, agentHeight);
    }

    glm::vec3 NavmeshServiceImpl::getClosestPointOnNavmesh(const glm::vec3& point, float searchRadius)
    {
        if (!navmeshProvider->hasNavmesh())
        {
            return point;
        }
        return navmeshProvider->getClosestPoint(point, searchRadius);
    }

    bool NavmeshServiceImpl::isPointOnNavmesh(const glm::vec3& point, float tolerance)
    {
        if (!navmeshProvider->hasNavmesh())
        {
            return false;
        }
        return navmeshProvider->isPointOnNavmesh(point, tolerance);
    }


    void NavmeshServiceImpl::addAgent(EntityHandle entity)
    {
        agentManager.addAgent(entity);
    }

    void NavmeshServiceImpl::removeAgent(EntityHandle entity)
    {
        agentManager.removeAgent(entity);
    }

    void NavmeshServiceImpl::setAgentDestination(EntityHandle entity, const glm::vec3& target)
    {
        agentManager.setAgentDestination(entity, target);
    }

    void NavmeshServiceImpl::stopAgent(EntityHandle entity)
    {
        agentManager.stopAgent(entity);
    }

    void NavmeshServiceImpl::update(float deltaTime, bool simulateAgents)
    {
        playModeActive = simulateAgents;
        // VK-1422: keep runtime navmesh tile rebuilds in-memory during play (no disk writes).
        tileManager.setPlayModeActive(simulateAgents);

        pollBakeCompletion();
        tileManager.pollTileBakeCompletions();

        // World bake is an edit-mode job — entering play mode aborts it
        if (simulateAgents)
            worldBaker->cancel();
        else
            worldBaker->update();

        gatherInvokerSources();
        auto streamResult = tileManager.updateStreaming();
        if (!streamResult.unloaded.empty())
            agentManager.suspendAgentsOnUnloadedTiles(streamResult.unloaded);
        if (!streamResult.loaded.empty())
            agentManager.resumeAgentsOnLoadedTiles(streamResult.loaded);
        tileManager.processDirtyTiles();
        tileManager.processOnDemandGeneration();
        if (simulateAgents)
            agentManager.updatePositions(deltaTime);
        drawOffMeshLinkDebug();
        trackOffMeshLinkTransforms();
        trackObstacleTransforms();
        drawObstacleDebug();
        trackModifierVolumeTransforms();
        drawModifierVolumeDebug();
        drawInvokerDebug();
    }

    void NavmeshServiceImpl::gatherInvokerSources()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::NavInvokerComponent, components::TransformComponent>();

        std::vector<StreamingSource> sources;
        for (auto entity : view)
        {
            auto& invoker = view.get<components::NavInvokerComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            invoker.isActive = navmeshProvider->hasNavmesh();

            float unloadRadius = invoker.generationRadius * invoker.unloadRadiusMultiplier;
            sources.push_back({
                transform.position,
                invoker.generationRadius * invoker.generationRadius,
                unloadRadius * unloadRadius
            });
        }

        if (!sources.empty())
        {
            tileManager.ensureTiledNavmeshInitialized();

            // Check if any invoker wants to save generated tiles to cache
            bool anySave = false;
            for (auto entity : view)
            {
                if (view.get<components::NavInvokerComponent>(entity).saveGeneratedToCache)
                {
                    anySave = true;
                    break;
                }
            }
            tileManager.setSaveOnDemandToCache(anySave);
        }

        tileManager.setInvokerSources(std::move(sources));
    }

    void NavmeshServiceImpl::drawInvokerDebug()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        bool showNavmesh = dispatcher.query(events::render::GetShowNavmeshDebugQuery{});
        if (!showNavmesh)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::NavInvokerComponent, components::TransformComponent>();

        for (auto entity : view)
        {
            const auto& invoker = view.get<components::NavInvokerComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            events::debugdraw::DrawSphereCommand sphereCmd;
            sphereCmd.center = transform.position;
            sphereCmd.radius = invoker.generationRadius;
            sphereCmd.color = {0.2f, 0.6f, 1.0f, 0.4f}; // Blue for invoker range
            dispatcher.execute(sphereCmd);
        }
    }


    void NavmeshServiceImpl::getNavmeshDebugMesh(std::vector<glm::vec3>& outVertices,
                                                   std::vector<uint32_t>& outIndices) const
    {
        navmeshProvider->getDebugMesh(outVertices, outIndices);
    }

    // === Per-Tile Delegations ===

    bool NavmeshServiceImpl::bakeSingleTile(int tileX, int tileZ)
    {
        return tileManager.bakeSingleTile(tileX, tileZ);
    }

    bool NavmeshServiceImpl::saveNavmeshTiled(const std::string& directory)
    {
        bool result = tileManager.saveNavmeshTiled(directory);
        if (result)
        {
            std::string indexPath = directory + "/index.vfNavIndex";
            attachNavmeshAssetToSceneRoot(indexPath);

            events::resource::AssetSavedNotification notif;
            notif.filePath = indexPath;
            ::events::EventDispatcher::instance().publish(notif);
        }
        return result;
    }

    bool NavmeshServiceImpl::loadNavmeshTiled(const std::string& directory)
    {
        bool success = tileManager.loadNavmeshTiled(directory, lastBakeSettings);
        if (success)
        {
            // Re-attach the asset ref so a manual load also links the navmesh
            // to the scene (e.g. after the scene was saved without it)
            attachNavmeshAssetToSceneRoot(directory + "/index.vfNavIndex");

            events::navmesh::NavmeshBakeCompleteNotification notification;
            notification.success = true;
            notification.message = "Navmesh loaded from tiled directory";
            ::events::EventDispatcher::instance().publish(notification);
        }
        return success;
    }

    void NavmeshServiceImpl::drawOffMeshLinkDebug()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        bool showNavmesh = dispatcher.query(events::render::GetShowNavmeshDebugQuery{});
        if (!showNavmesh)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::OffMeshLinkComponent, components::TransformComponent>();

        for (auto entity : view)
        {
            const auto& link = view.get<components::OffMeshLinkComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            glm::vec3 worldStart = transform.position + link.startOffset;
            glm::vec3 worldEnd = transform.position + link.endOffset;

            // Color based on link type
            glm::vec4 lineColor;
            switch (link.linkType)
            {
                case components::OffMeshLinkType::Jump:  lineColor = {0.0f, 1.0f, 0.0f, 1.0f}; break;
                case components::OffMeshLinkType::Climb: lineColor = {0.0f, 0.5f, 1.0f, 1.0f}; break;
                case components::OffMeshLinkType::Drop:  lineColor = {1.0f, 0.5f, 0.0f, 1.0f}; break;
                case components::OffMeshLinkType::Custom: lineColor = {1.0f, 1.0f, 0.0f, 1.0f}; break;
            }

            // Draw line connecting start and end
            events::debugdraw::DrawLineCommand lineCmd;
            lineCmd.start = worldStart;
            lineCmd.end = worldEnd;
            lineCmd.color = lineColor;
            dispatcher.execute(lineCmd);

            // Draw spheres at endpoints
            events::debugdraw::DrawSphereCommand startSphere;
            startSphere.center = worldStart;
            startSphere.radius = link.radius * 0.5f;
            startSphere.color = {0.0f, 1.0f, 0.0f, 1.0f}; // Green = start
            dispatcher.execute(startSphere);

            glm::vec4 endColor = link.direction == components::OffMeshLinkDirection::Bidirectional
                                    ? glm::vec4{0.0f, 1.0f, 0.0f, 1.0f}   // Green = bidirectional
                                    : glm::vec4{1.0f, 0.0f, 0.0f, 1.0f};  // Red = one-way endpoint
            events::debugdraw::DrawSphereCommand endSphere;
            endSphere.center = worldEnd;
            endSphere.radius = link.radius * 0.5f;
            endSphere.color = endColor;
            dispatcher.execute(endSphere);

            // Draw direction arrow (midpoint line segment)
            if (link.direction == components::OffMeshLinkDirection::OneWay)
            {
                glm::vec3 mid = (worldStart + worldEnd) * 0.5f;
                glm::vec3 dir = glm::normalize(worldEnd - worldStart);
                glm::vec3 arrowTip = mid + dir * 0.3f;
                events::debugdraw::DrawLineCommand arrowCmd;
                arrowCmd.start = mid - dir * 0.3f;
                arrowCmd.end = arrowTip;
                arrowCmd.color = lineColor;
                dispatcher.execute(arrowCmd);
            }
        }
    }

    void NavmeshServiceImpl::trackOffMeshLinkTransforms()
    {
        if (!navmeshProvider->hasNavmesh())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::OffMeshLinkComponent, components::TransformComponent>();

        // Track live entity IDs
        std::unordered_set<uint64_t> liveIds;

        for (auto entity : view)
        {
            const auto& transform = view.get<components::TransformComponent>(entity);
            uint64_t id = static_cast<uint64_t>(entity);
            liveIds.insert(id);

            auto it = lastOffMeshLinkPositions.find(id);
            if (it == lastOffMeshLinkPositions.end())
            {
                lastOffMeshLinkPositions[id] = transform.position;
            }
            else if (it->second != transform.position)
            {
                const auto& link = view.get<components::OffMeshLinkComponent>(entity);
                glm::vec3 oldStart = it->second + link.startOffset;
                glm::vec3 newStart = transform.position + link.startOffset;

                auto oldCoord = tileManager.worldToTileCoord(oldStart);
                auto newCoord = tileManager.worldToTileCoord(newStart);

                tileManager.markTileDirty(oldCoord.x, oldCoord.z);
                if (oldCoord != newCoord)
                    tileManager.markTileDirty(newCoord.x, newCoord.z);

                it->second = transform.position;
            }
        }

        // Remove stale entries for destroyed entities
        for (auto it = lastOffMeshLinkPositions.begin(); it != lastOffMeshLinkPositions.end(); )
        {
            if (liveIds.find(it->first) == liveIds.end())
                it = lastOffMeshLinkPositions.erase(it);
            else
                ++it;
        }
    }

    // === Dynamic Obstacle Support ===

    void NavmeshServiceImpl::trackObstacleTransforms()
    {
        if (!navmeshProvider->hasNavmesh())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::NavmeshObstacleComponent, components::TransformComponent>();

        float tileWorldSize = lastBakeSettings.tileSize * lastBakeSettings.cellSize;

        std::unordered_set<uint64_t> liveIds;

        for (auto entity : view)
        {
            auto& obstacle = view.get<components::NavmeshObstacleComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);
            uint64_t id = static_cast<uint64_t>(entity);
            liveIds.insert(id);

            glm::vec3 worldPos = transform.position + obstacle.offset;

            // Register avoidance-only phantom agents on first sight
            if (obstacle.mode == components::NavmeshObstacleMode::AvoidanceOnly && !obstacle.isRegistered)
            {
                float obstacleRadius = (obstacle.shape == components::NavmeshObstacleShape::Box)
                    ? glm::max(obstacle.size.x, obstacle.size.z) * 0.5f
                    : obstacle.size.x;
                int idx = navmeshProvider->addCrowdAgent(worldPos, obstacleRadius, obstacle.size.y, 0.0f, 0.0f);
                obstacle.phantomAgentIndex = idx;
                obstacle.isRegistered = true;
                obstacle.lastBakedPosition = worldPos;
                lastObstaclePositions[id] = worldPos;
                continue;
            }

            auto it = lastObstaclePositions.find(id);
            if (it == lastObstaclePositions.end())
            {
                lastObstaclePositions[id] = worldPos;
                obstacle.lastBakedPosition = worldPos;
                continue;
            }

            float dist = glm::distance(worldPos, obstacle.lastBakedPosition);
            if (dist < obstacle.movementThreshold)
                continue;

            if (obstacle.mode == components::NavmeshObstacleMode::Carve)
            {
                glm::vec3 half = (obstacle.shape == components::NavmeshObstacleShape::Box)
                    ? obstacle.size * 0.5f
                    : glm::vec3(obstacle.size.x, obstacle.size.y * 0.5f, obstacle.size.x);

                auto dirtyRange = [&](const glm::vec3& pos)
                {
                    auto coordMin = tileManager.worldToTileCoord(pos - half);
                    auto coordMax = tileManager.worldToTileCoord(pos + half);
                    for (int tx = coordMin.x; tx <= coordMax.x; ++tx)
                        for (int tz = coordMin.z; tz <= coordMax.z; ++tz)
                            tileManager.markTileDirty(tx, tz);
                };

                dirtyRange(obstacle.lastBakedPosition);
                dirtyRange(worldPos);
            }
            else if (obstacle.mode == components::NavmeshObstacleMode::AvoidanceOnly)
            {
                if (obstacle.phantomAgentIndex >= 0)
                    navmeshProvider->removeCrowdAgent(obstacle.phantomAgentIndex);

                float obstacleRadius = (obstacle.shape == components::NavmeshObstacleShape::Box)
                    ? glm::max(obstacle.size.x, obstacle.size.z) * 0.5f
                    : obstacle.size.x;
                int idx = navmeshProvider->addCrowdAgent(worldPos, obstacleRadius, obstacle.size.y, 0.0f, 0.0f);
                obstacle.phantomAgentIndex = idx;
            }

            obstacle.lastBakedPosition = worldPos;
            it->second = worldPos;
        }

        // Clean up stale entries and phantom agents for destroyed entities
        for (auto it = lastObstaclePositions.begin(); it != lastObstaclePositions.end(); )
        {
            if (liveIds.find(it->first) == liveIds.end())
            {
                // Check if this was a phantom agent that needs cleanup
                auto enttEntity = static_cast<entt::entity>(it->first);
                if (registry.valid(enttEntity) && registry.all_of<components::NavmeshObstacleComponent>(enttEntity))
                {
                    auto& obs = registry.get<components::NavmeshObstacleComponent>(enttEntity);
                    if (obs.phantomAgentIndex >= 0)
                    {
                        navmeshProvider->removeCrowdAgent(obs.phantomAgentIndex);
                        obs.phantomAgentIndex = -1;
                    }
                }
                it = lastObstaclePositions.erase(it);
            }
            else
                ++it;
        }
    }

    void NavmeshServiceImpl::cleanupPhantomAgents()
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::NavmeshObstacleComponent>();

        for (auto entity : view)
        {
            auto& obstacle = view.get<components::NavmeshObstacleComponent>(entity);
            if (obstacle.phantomAgentIndex >= 0)
            {
                navmeshProvider->removeCrowdAgent(obstacle.phantomAgentIndex);
                obstacle.phantomAgentIndex = -1;
            }
            obstacle.isRegistered = false;
        }
    }

    void NavmeshServiceImpl::drawObstacleDebug()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        bool showNavmesh = dispatcher.query(events::render::GetShowNavmeshDebugQuery{});
        if (!showNavmesh)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::NavmeshObstacleComponent, components::TransformComponent>();

        for (auto entity : view)
        {
            const auto& obstacle = view.get<components::NavmeshObstacleComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            glm::vec3 worldPos = transform.position + obstacle.offset;

            glm::vec4 color = (obstacle.mode == components::NavmeshObstacleMode::Carve)
                ? glm::vec4{1.0f, 0.2f, 0.2f, 0.8f}   // Red for carving
                : glm::vec4{1.0f, 1.0f, 0.0f, 0.8f};  // Yellow for avoidance-only

            if (obstacle.shape == components::NavmeshObstacleShape::Box)
            {
                events::debugdraw::DrawBoxCommand boxCmd;
                boxCmd.center = worldPos;
                boxCmd.halfExtents = obstacle.size * 0.5f;
                boxCmd.color = color;
                dispatcher.execute(boxCmd);
            }
            else
            {
                // Approximate cylinder with a box matching its bounding extents
                events::debugdraw::DrawBoxCommand boxCmd;
                boxCmd.center = worldPos;
                boxCmd.halfExtents = glm::vec3(obstacle.size.x, obstacle.size.y * 0.5f, obstacle.size.x);
                boxCmd.color = color;
                dispatcher.execute(boxCmd);
            }
        }
    }

    // === Modifier Volume Support ===

    void NavmeshServiceImpl::trackModifierVolumeTransforms()
    {
        if (!navmeshProvider->hasNavmesh())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::NavmeshModifierVolumeComponent, components::TransformComponent>();

        std::unordered_set<uint64_t> liveIds;

        for (auto entity : view)
        {
            const auto& volume = view.get<components::NavmeshModifierVolumeComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);
            uint64_t id = static_cast<uint64_t>(entity);
            liveIds.insert(id);

            glm::vec3 worldPos = transform.position + volume.offset;

            auto it = lastModifierVolumePositions.find(id);
            if (it == lastModifierVolumePositions.end())
            {
                lastModifierVolumePositions[id] = worldPos;
            }
            else if (it->second != worldPos)
            {
                glm::vec3 half = (volume.shape == components::NavmeshModifierVolumeShape::Box)
                    ? volume.size * 0.5f
                    : glm::vec3(volume.size.x, volume.size.y * 0.5f, volume.size.x);

                auto dirtyRange = [&](const glm::vec3& pos)
                {
                    auto coordMin = tileManager.worldToTileCoord(pos - half);
                    auto coordMax = tileManager.worldToTileCoord(pos + half);
                    for (int tx = coordMin.x; tx <= coordMax.x; ++tx)
                        for (int tz = coordMin.z; tz <= coordMax.z; ++tz)
                            tileManager.markTileDirty(tx, tz);
                };

                dirtyRange(it->second);
                dirtyRange(worldPos);

                it->second = worldPos;
            }
        }

        // Remove stale entries for destroyed entities
        for (auto it = lastModifierVolumePositions.begin(); it != lastModifierVolumePositions.end(); )
        {
            if (liveIds.find(it->first) == liveIds.end())
                it = lastModifierVolumePositions.erase(it);
            else
                ++it;
        }
    }

    void NavmeshServiceImpl::drawModifierVolumeDebug()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();
        bool showNavmesh = dispatcher.query(events::render::GetShowNavmeshDebugQuery{});
        if (!showNavmesh)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto view = registry.view<components::NavmeshModifierVolumeComponent, components::TransformComponent>();

        for (auto entity : view)
        {
            const auto& volume = view.get<components::NavmeshModifierVolumeComponent>(entity);
            const auto& transform = view.get<components::TransformComponent>(entity);

            glm::vec3 worldPos = transform.position + volume.offset;

            // Cyan/teal color for modifier volumes
            glm::vec4 color{0.0f, 0.8f, 0.8f, 0.6f};

            if (volume.shape == components::NavmeshModifierVolumeShape::Box)
            {
                events::debugdraw::DrawBoxCommand boxCmd;
                boxCmd.center = worldPos;
                boxCmd.halfExtents = volume.size * 0.5f;
                boxCmd.color = color;
                dispatcher.execute(boxCmd);
            }
            else
            {
                events::debugdraw::DrawSphereCommand sphereCmd;
                sphereCmd.center = worldPos;
                sphereCmd.radius = volume.size.x;
                sphereCmd.color = color;
                dispatcher.execute(sphereCmd);
            }
        }
    }

}
