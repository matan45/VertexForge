#include "NavmeshServiceImpl.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "../../events/terrain/TerrainEvents.hpp"
#include "../../events/terrain/BrushEvents.hpp"
#include "../../events/terrain/HoleBrushEvents.hpp"
#include "../../events/render/RenderEvents.hpp"
#include "../../events/project/ResourceEvents.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "navigation/NavmeshSerializer.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"
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
                agentManager.updateAgentConfig(cmd.entity, cmd.maxSpeed, cmd.maxAcceleration);
            });

        dispatcher.registerQueryHandler<events::navmesh::GetAgentVelocityQuery>(
            [this](const events::navmesh::GetAgentVelocityQuery& query)
            {
                return agentManager.getAgentVelocity(query.entity);
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

        bakeFuture = threading::JobSystem::instance().submit(
            [this, geom = std::move(geometry), settings]()
            {
                return navmeshProvider->buildNavmesh(geom, settings);
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
            auto& registry = scene::EntityRegistry::getRegistry();
            auto rootHandle = ::events::EventDispatcher::instance().query(::events::scene::GetRootEntityQuery{});
            if (rootHandle.isValid())
            {
                auto rootEntity = internal::fromHandle(rootHandle);
                registry.emplace_or_replace<components::NavmeshComponent>(rootEntity,
                    components::NavmeshComponent{filePath});
            }

            events::resource::AssetSavedNotification notif;
            notif.filePath = filePath;
            ::events::EventDispatcher::instance().publish(notif);
        }
        return result;
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
        navmeshProvider->clearNavmesh();
        agentManager.clear();
        tileManager.clear();

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

    void NavmeshServiceImpl::updateAgents(float deltaTime)
    {
        pollBakeCompletion();
        tileManager.pollTileBakeCompletions();
        auto streamResult = tileManager.updateStreaming();
        if (!streamResult.unloaded.empty())
            agentManager.suspendAgentsOnUnloadedTiles(streamResult.unloaded);
        if (!streamResult.loaded.empty())
            agentManager.resumeAgentsOnLoadedTiles(streamResult.loaded);
        tileManager.processDirtyTiles();
        agentManager.updatePositions(deltaTime);
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
        return tileManager.saveNavmeshTiled(directory);
    }

    bool NavmeshServiceImpl::loadNavmeshTiled(const std::string& directory)
    {
        return tileManager.loadNavmeshTiled(directory, lastBakeSettings);
    }

}
