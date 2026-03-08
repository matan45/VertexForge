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
#include <cmath>

namespace services
{
    NavmeshServiceImpl::NavmeshServiceImpl(INavmeshProvider* navmeshProvider)
        : navmeshProvider(navmeshProvider)
    {
        assert(navmeshProvider && "NavmeshProvider must not be null");
    }

    NavmeshServiceImpl::~NavmeshServiceImpl()
    {
        if (bakeFuture.valid())
        {
            bakeFuture.wait();
        }
        for (auto& pending : pendingTileBakes)
        {
            if (pending.future.valid())
                pending.future.wait();
        }

        auto& dispatcher = ::events::EventDispatcher::instance();
        if (brushAppliedToken.isValid())
            dispatcher.unsubscribe(brushAppliedToken);
        if (holeBrushAppliedToken.isValid())
            dispatcher.unsubscribe(holeBrushAppliedToken);
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
                streamer.setConfig(cmd.config);
            });

        dispatcher.registerCommandHandler<events::navmesh::SetNavmeshStreamingEnabledCommand>(
            [this](const events::navmesh::SetNavmeshStreamingEnabledCommand& cmd)
            {
                streamer.setEnabled(cmd.enabled);
            });

        dispatcher.registerQueryHandler<events::navmesh::GetNavmeshStreamingConfigQuery>(
            [this](const events::navmesh::GetNavmeshStreamingConfigQuery&)
            {
                return streamer.getConfig();
            });

        dispatcher.registerQueryHandler<events::navmesh::IsNavmeshStreamingEnabledQuery>(
            [this](const events::navmesh::IsNavmeshStreamingEnabledQuery&)
            {
                return streamer.isEnabled();
            });

        dispatcher.registerQueryHandler<events::navmesh::GetNavmeshTileStatusQuery>(
            [this](const events::navmesh::GetNavmeshTileStatusQuery&)
            {
                std::vector<events::navmesh::NavmeshTileStatusInfo> result;
                if (tileCache)
                {
                    tileCache->forEachTile([&](const navigation::NavmeshTileCoord& coord)
                    {
                        events::navmesh::NavmeshTileStatusInfo info;
                        info.coord = coord;
                        if (dirtyTiles.count(coord))
                            info.status = events::navmesh::NavmeshTileStatus::Dirty;
                        else if (streamer.isTileLoaded(coord))
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
                lastCameraPos = notif.position;
            });

        // Subscribe to terrain brush events for incremental rebake (VK-778)
        brushAppliedToken = dispatcher.subscribe<events::brush::BrushAppliedNotification>(
            [this](const events::brush::BrushAppliedNotification& notif)
            {
                if (!navmeshProvider->hasNavmesh())
                    return;
                auto coord = worldToTileCoord(notif.position);
                markTileDirty(coord.x, coord.z);
                // Also mark neighbors for border overlap
                markTileDirty(coord.x - 1, coord.z);
                markTileDirty(coord.x + 1, coord.z);
                markTileDirty(coord.x, coord.z - 1);
                markTileDirty(coord.x, coord.z + 1);
            });

        holeBrushAppliedToken = dispatcher.subscribe<events::holeBrush::HoleBrushAppliedNotification>(
            [this](const events::holeBrush::HoleBrushAppliedNotification& notif)
            {
                if (!navmeshProvider->hasNavmesh())
                    return;
                auto coord = worldToTileCoord(notif.position);
                markTileDirty(coord.x, coord.z);
                markTileDirty(coord.x - 1, coord.z);
                markTileDirty(coord.x + 1, coord.z);
                markTileDirty(coord.x, coord.z - 1);
                markTileDirty(coord.x, coord.z + 1);
            });
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
            // Store navmesh path on root entity (like IBL) so it persists with scene save
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
        entityToAgentIndex.clear();
        dirtyTiles.clear();
        pendingTileBakes.clear();
        suspendedAgents.clear();
        streamer.clear();
        tileCache.reset();

        // Remove NavmeshComponent from root entity
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
        if (entityToAgentIndex.count(entity.id))
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = internal::fromHandle(entity);
        if (!registry.valid(enttEntity))
        {
            return;
        }

        float radius = 0.3f;
        float height = 2.0f;
        float maxSpeed = 3.5f;
        float maxAcceleration = 8.0f;

        if (registry.all_of<components::NavmeshAgentComponent>(enttEntity))
        {
            const auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);
            radius = agent.radius;
            height = agent.height;
            maxSpeed = agent.maxSpeed;
            maxAcceleration = agent.maxAcceleration;
        }

        glm::vec3 position{0.0f};
        if (registry.all_of<components::TransformComponent>(enttEntity))
        {
            position = registry.get<components::TransformComponent>(enttEntity).position;
        }

        int agentIdx = navmeshProvider->addCrowdAgent(position, radius, height, maxSpeed, maxAcceleration);
        if (agentIdx >= 0)
        {
            entityToAgentIndex[entity.id] = agentIdx;

            if (registry.all_of<components::NavmeshAgentComponent>(enttEntity))
            {
                auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);
                agent.isActive = true;
                agent.crowdAgentIndex = agentIdx;
            }
        }
    }

    void NavmeshServiceImpl::removeAgent(EntityHandle entity)
    {
        auto it = entityToAgentIndex.find(entity.id);
        if (it != entityToAgentIndex.end())
        {
            navmeshProvider->removeCrowdAgent(it->second);

            auto& registry = scene::EntityRegistry::getRegistry();
            auto enttEntity = internal::fromHandle(entity);
            if (registry.valid(enttEntity) && registry.all_of<components::NavmeshAgentComponent>(enttEntity))
            {
                auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);
                agent.isActive = false;
                agent.crowdAgentIndex = -1;
            }

            entityToAgentIndex.erase(it);
        }
    }

    void NavmeshServiceImpl::setAgentDestination(EntityHandle entity, const glm::vec3& target)
    {
        auto it = entityToAgentIndex.find(entity.id);
        if (it != entityToAgentIndex.end())
        {
            navmeshProvider->setCrowdAgentTarget(it->second, target);
        }
    }

    void NavmeshServiceImpl::stopAgent(EntityHandle entity)
    {
        auto it = entityToAgentIndex.find(entity.id);
        if (it != entityToAgentIndex.end())
        {
            navmeshProvider->stopCrowdAgent(it->second);
        }
    }

    void NavmeshServiceImpl::updateAgents(float deltaTime)
    {
        pollBakeCompletion();
        pollTileBakeCompletions();
        updateStreaming();
        processDirtyTiles();

        if (entityToAgentIndex.empty())
        {
            return;
        }

        navmeshProvider->updateCrowd(deltaTime);

        auto& registry = scene::EntityRegistry::getRegistry();
        for (const auto& [entityId, agentIdx] : entityToAgentIndex)
        {
            EntityHandle handle{entityId};
            auto enttEntity = internal::fromHandle(handle);
            if (!registry.valid(enttEntity) || !registry.all_of<components::TransformComponent>(enttEntity))
            {
                continue;
            }

            glm::vec3 agentPos = navmeshProvider->getCrowdAgentPosition(agentIdx);
            auto& transform = registry.get<components::TransformComponent>(enttEntity);
            transform.position = agentPos;
            transform.isDirty = true;

        }
    }


    void NavmeshServiceImpl::getNavmeshDebugMesh(std::vector<glm::vec3>& outVertices,
                                                   std::vector<uint32_t>& outIndices) const
    {
        navmeshProvider->getDebugMesh(outVertices, outIndices);
    }

    // === Per-Tile Operations (VK-739) ===

    navigation::NavmeshTileCoord NavmeshServiceImpl::worldToTileCoord(const glm::vec3& worldPos) const
    {
        float tileWorldSize = lastBakeSettings.tileSize * lastBakeSettings.cellSize;
        return {
            static_cast<int32_t>(std::floor(worldPos.x / tileWorldSize)),
            static_cast<int32_t>(std::floor(worldPos.z / tileWorldSize))
        };
    }

    void NavmeshServiceImpl::markTileDirty(int tileX, int tileZ)
    {
        dirtyTiles.insert({tileX, tileZ});
    }

    bool NavmeshServiceImpl::bakeSingleTile(int tileX, int tileZ)
    {
        if (!navmeshProvider->hasNavmesh())
            return false;

        navigation::NavmeshTileCoord coord{tileX, tileZ};
        auto bounds = navigation::computeTileBounds(coord, lastBakeSettings, -1000.0f, 1000.0f);

        navigation::NavmeshInputGeometry geometry;
        collectTileGeometry(bounds, lastBakeSettings, geometry);

        if (geometry.isEmpty())
        {
            navmeshProvider->removeNavmeshTile(tileX, tileZ);
            return true;
        }

        auto tileData = navmeshProvider->buildSingleTile(tileX, tileZ, geometry, lastBakeSettings);
        if (tileData.data.empty())
            return false;

        navmeshProvider->addNavmeshTile(tileData);

        if (tileCache)
        {
            tileCache->saveTile(coord, tileData);
        }

        dirtyTiles.erase(coord);

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::navmesh::NavmeshTileUpdatedNotification notif;
        notif.tileX = tileX;
        notif.tileZ = tileZ;
        dispatcher.publish(notif);

        return true;
    }

    bool NavmeshServiceImpl::saveNavmeshTiled(const std::string& directory)
    {
        if (!navmeshProvider->hasNavmesh())
            return false;

        tileCache = std::make_unique<navigation::NavmeshTileCache>(directory);

        auto tiles = navmeshProvider->serializeNavmesh();

        navigation::NavmeshTileIndex index;
        index.settings = lastBakeSettings;

        for (const auto& tile : tiles)
        {
            navigation::NavmeshTileCoord coord{tile.x, tile.y};
            tileCache->saveTile(coord, tile);
            index.tileCoords.push_back(coord);
        }

        // Compute bounds from settings
        if (!tiles.empty())
        {
            float tileWorldSize = lastBakeSettings.tileSize * lastBakeSettings.cellSize;
            float minX = 1e9f, minZ = 1e9f, maxX = -1e9f, maxZ = -1e9f;
            for (const auto& coord : index.tileCoords)
            {
                minX = std::min(minX, coord.x * tileWorldSize);
                minZ = std::min(minZ, coord.z * tileWorldSize);
                maxX = std::max(maxX, (coord.x + 1) * tileWorldSize);
                maxZ = std::max(maxZ, (coord.z + 1) * tileWorldSize);
            }
            index.boundsMin = glm::vec3(minX, -1000.0f, minZ);
            index.boundsMax = glm::vec3(maxX, 1000.0f, maxZ);
        }

        tileCache->saveIndex(index);

        // Setup streamer
        streamer.setTileCache(tileCache.get());
        streamer.setProvider(navmeshProvider);
        streamer.setSettings(lastBakeSettings);

        vfLogInfo("NavmeshService: Saved {} tiles to {}", tiles.size(), directory);
        return true;
    }

    bool NavmeshServiceImpl::loadNavmeshTiled(const std::string& directory)
    {
        tileCache = std::make_unique<navigation::NavmeshTileCache>(directory);

        navigation::NavmeshTileIndex index;
        if (!tileCache->loadIndex(index))
        {
            vfLogError("NavmeshService: Failed to load navmesh tile index from {}", directory);
            return false;
        }

        lastBakeSettings = index.settings;

        if (!navmeshProvider->initTiledNavmesh(index.settings, index.boundsMin, index.boundsMax))
        {
            vfLogError("NavmeshService: Failed to init tiled navmesh");
            return false;
        }

        // Load all tiles immediately into the dtNavMesh
        int loadedCount = 0;
        for (const auto& coord : index.tileCoords)
        {
            navigation::NavmeshTileData tileData;
            if (tileCache->loadTile(coord, tileData))
            {
                if (navmeshProvider->addNavmeshTile(tileData))
                {
                    loadedCount++;
                }
            }
        }

        // Setup streamer for runtime streaming
        streamer.setTileCache(tileCache.get());
        streamer.setProvider(navmeshProvider);
        streamer.setSettings(lastBakeSettings);
        streamer.setEnabled(true);

        // Mark all loaded tiles in the streamer so it knows they're already in memory
        for (const auto& coord : index.tileCoords)
        {
            streamer.markTileLoaded(coord);
        }

        vfLogInfo("NavmeshService: Loaded {} / {} tiles from {}", loadedCount, index.tileCoords.size(), directory);

        auto& dispatcher = ::events::EventDispatcher::instance();
        events::navmesh::NavmeshBakeCompleteNotification notification;
        notification.success = true;
        notification.message = "Tiled navmesh loaded";
        dispatcher.publish(notification);

        return true;
    }

    // === Streaming Update (VK-777) ===

    void NavmeshServiceImpl::updateStreaming()
    {
        if (!streamer.isEnabled())
            return;

        std::vector<navigation::NavmeshTileCoord> loaded;
        std::vector<navigation::NavmeshTileCoord> unloaded;

        streamer.update(lastCameraPos, loaded, unloaded);

        auto& dispatcher = ::events::EventDispatcher::instance();

        for (const auto& coord : loaded)
        {
            events::navmesh::NavmeshTileLoadedNotification notif;
            notif.tileX = coord.x;
            notif.tileZ = coord.z;
            dispatcher.publish(notif);
        }

        for (const auto& coord : unloaded)
        {
            events::navmesh::NavmeshTileUnloadedNotification notif;
            notif.tileX = coord.x;
            notif.tileZ = coord.z;
            dispatcher.publish(notif);
        }

        // Agent suspend/resume (VK-779)
        if (!unloaded.empty())
        {
            suspendAgentsOnUnloadedTiles(unloaded);
        }
        if (!loaded.empty())
        {
            resumeAgentsOnLoadedTiles(loaded);
        }
    }

    // === Dirty Tile Processing (VK-778) ===

    void NavmeshServiceImpl::processDirtyTiles()
    {
        if (dirtyTiles.empty())
            return;

        // Poll completions first
        pollTileBakeCompletions();

        // Submit new bakes up to budget
        int submitted = 0;
        auto it = dirtyTiles.begin();
        while (it != dirtyTiles.end() && submitted < MAX_TILE_BAKES_PER_FRAME)
        {
            // Don't submit if we already have too many pending
            if (static_cast<int>(pendingTileBakes.size()) >= MAX_TILE_BAKES_PER_FRAME)
                break;

            navigation::NavmeshTileCoord coord = *it;
            it = dirtyTiles.erase(it);

            auto bounds = navigation::computeTileBounds(coord, lastBakeSettings, -1000.0f, 1000.0f);

            navigation::NavmeshInputGeometry geometry;
            collectTileGeometry(bounds, lastBakeSettings, geometry);

            if (geometry.isEmpty())
            {
                navmeshProvider->removeNavmeshTile(coord.x, coord.z);
                continue;
            }

            auto future = threading::JobSystem::instance().submit(
                [this, coord, geom = std::move(geometry), settings = lastBakeSettings]()
                {
                    return navmeshProvider->buildSingleTile(coord.x, coord.z, geom, settings);
                }, threading::JobPriority::NORMAL);

            pendingTileBakes.push_back({coord, std::move(future)});
            submitted++;
        }
    }

    void NavmeshServiceImpl::pollTileBakeCompletions()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        auto it = pendingTileBakes.begin();
        while (it != pendingTileBakes.end())
        {
            if (it->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
            {
                auto tileData = it->future.get();
                if (!tileData.data.empty())
                {
                    navmeshProvider->addNavmeshTile(tileData);

                    if (tileCache)
                    {
                        tileCache->saveTile(it->coord, tileData);
                    }

                    events::navmesh::NavmeshTileUpdatedNotification notif;
                    notif.tileX = it->coord.x;
                    notif.tileZ = it->coord.z;
                    dispatcher.publish(notif);
                }

                it = pendingTileBakes.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    // === Agent Suspend/Resume (VK-779) ===

    void NavmeshServiceImpl::suspendAgentsOnUnloadedTiles(
        const std::vector<navigation::NavmeshTileCoord>& unloadedTiles)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> unloadedSet(
            unloadedTiles.begin(), unloadedTiles.end());

        auto toRemove = std::vector<uint64_t>();

        for (const auto& [entityId, agentIdx] : entityToAgentIndex)
        {
            glm::vec3 agentPos = navmeshProvider->getCrowdAgentPosition(agentIdx);
            auto tileCoord = worldToTileCoord(agentPos);

            if (unloadedSet.count(tileCoord))
            {
                SuspendedAgent suspended;
                suspended.entityId = entityId;
                suspended.position = agentPos;
                suspended.target = glm::vec3(0.0f);
                suspended.hasTarget = false;

                auto enttEntity = internal::fromHandle(EntityHandle{entityId});
                if (registry.valid(enttEntity) && registry.all_of<components::NavmeshAgentComponent>(enttEntity))
                {
                    auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);
                    agent.isSuspended = true;
                    agent.suspendedPosition = agentPos;
                }

                navmeshProvider->removeCrowdAgent(agentIdx);
                suspendedAgents.push_back(suspended);
                toRemove.push_back(entityId);
            }
        }

        for (uint64_t id : toRemove)
        {
            entityToAgentIndex.erase(id);
        }
    }

    void NavmeshServiceImpl::resumeAgentsOnLoadedTiles(
        const std::vector<navigation::NavmeshTileCoord>& loadedTiles)
    {
        auto& registry = scene::EntityRegistry::getRegistry();

        std::unordered_set<navigation::NavmeshTileCoord, navigation::NavmeshTileCoordHash> loadedSet(
            loadedTiles.begin(), loadedTiles.end());

        auto it = suspendedAgents.begin();
        while (it != suspendedAgents.end())
        {
            auto tileCoord = worldToTileCoord(it->position);
            if (loadedSet.count(tileCoord))
            {
                auto enttEntity = internal::fromHandle(EntityHandle{it->entityId});
                if (registry.valid(enttEntity) && registry.all_of<components::NavmeshAgentComponent>(enttEntity))
                {
                    auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);

                    int agentIdx = navmeshProvider->addCrowdAgent(
                        it->position, agent.radius, agent.height, agent.maxSpeed, agent.maxAcceleration);

                    if (agentIdx >= 0)
                    {
                        entityToAgentIndex[it->entityId] = agentIdx;
                        agent.isActive = true;
                        agent.isSuspended = false;
                        agent.crowdAgentIndex = agentIdx;

                        if (it->hasTarget)
                        {
                            navmeshProvider->setCrowdAgentTarget(agentIdx, it->target);
                        }
                    }
                }

                it = suspendedAgents.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

}
