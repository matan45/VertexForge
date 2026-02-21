#include "NavmeshServiceImpl.hpp"
#include "../events/NavmeshEvents.hpp"
#include "../events/TerrainEvents.hpp"
#include "../events/RenderEvents.hpp"
#include "../events/ResourceEvents.hpp"
#include "../events/EventDispatcher.hpp"
#include "../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "navigation/NavmeshSerializer.hpp"
#include "resource/ResourceManager.hpp"
#include "resource/Types.hpp"
#include "print/EditorLogger.hpp"
#include <cassert>

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

        bakeFuture = std::async(std::launch::async,
            [this, geom = std::move(geometry), settings]()
            {
                return navmeshProvider->buildNavmesh(geom, settings);
            });
    }

    types::NavmeshBakeProgress NavmeshServiceImpl::getBakeProgress() const
    {
        if (bakeFuture.valid())
        {
            auto status = bakeFuture.wait_for(std::chrono::seconds(0));
            if (status == std::future_status::ready)
            {
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
        }

        return navmeshProvider->getBuildProgress();
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

}
