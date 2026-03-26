#include "VolumetricAgentManager.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/volumetric/VolumetricNavEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"

namespace services
{
    VolumetricAgentManager::VolumetricAgentManager(IVolumetricNavProvider* provider)
        : provider(provider)
    {
    }

    void VolumetricAgentManager::addAgent(EntityHandle entity)
    {
        if (agents.count(entity.id))
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = internal::fromHandle(entity);
        if (!registry.valid(enttEntity))
        {
            return;
        }

        agents[entity.id] = AgentState{};

        if (registry.all_of<components::VolumetricAgentComponent>(enttEntity))
        {
            auto& agent = registry.get<components::VolumetricAgentComponent>(enttEntity);
            agent.isActive = true;
        }
    }

    void VolumetricAgentManager::removeAgent(EntityHandle entity)
    {
        auto it = agents.find(entity.id);
        if (it != agents.end())
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            auto enttEntity = internal::fromHandle(entity);
            if (registry.valid(enttEntity) && registry.all_of<components::VolumetricAgentComponent>(enttEntity))
            {
                auto& agent = registry.get<components::VolumetricAgentComponent>(enttEntity);
                agent.isActive = false;
            }

            agents.erase(it);
        }
    }

    void VolumetricAgentManager::setDestination(EntityHandle entity, const glm::vec3& target)
    {
        auto it = agents.find(entity.id);
        if (it == agents.end())
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = internal::fromHandle(entity);
        if (!registry.valid(enttEntity) || !registry.all_of<components::TransformComponent>(enttEntity))
        {
            return;
        }

        glm::vec3 position = registry.get<components::TransformComponent>(enttEntity).position;
        auto path = provider->findPath3D(position, target);
        if (path.isValid && path.hasResults())
        {
            it->second.path = std::move(path);
            it->second.currentIndex = 0;
            it->second.velocity = glm::vec3(0.0f);
        }
    }

    void VolumetricAgentManager::stopAgent(EntityHandle entity)
    {
        auto it = agents.find(entity.id);
        if (it != agents.end())
        {
            it->second.path = volumetric::VolumePath{};
            it->second.currentIndex = 0;
            it->second.velocity = glm::vec3(0.0f);
        }
    }

    void VolumetricAgentManager::updatePositions(float deltaTime)
    {
        if (agents.empty())
        {
            return;
        }

        auto& registry = scene::EntityRegistry::getRegistry();

        for (auto& [entityId, state] : agents)
        {
            if (!state.path.isValid || state.path.waypoints.empty())
            {
                continue;
            }

            EntityHandle handle{entityId};
            auto enttEntity = internal::fromHandle(handle);
            if (!registry.valid(enttEntity) || !registry.all_of<components::TransformComponent>(enttEntity))
            {
                continue;
            }

            auto& transform = registry.get<components::TransformComponent>(enttEntity);

            float maxSpeed = 3.5f;
            float maxAcceleration = 8.0f;
            float arrivalDist = ARRIVAL_DISTANCE;

            if (registry.all_of<components::VolumetricAgentComponent>(enttEntity))
            {
                const auto& agentComp = registry.get<components::VolumetricAgentComponent>(enttEntity);
                maxSpeed = agentComp.maxSpeed;
                maxAcceleration = agentComp.maxAcceleration;
                arrivalDist = agentComp.stoppingDistance;
            }

            if (state.currentIndex >= state.path.waypoints.size())
            {
                // Reached end of path
                state.path = volumetric::VolumePath{};
                state.velocity = glm::vec3(0.0f);

                events::volumetric::VolumetricAgentReachedDestinationNotification notif;
                notif.entity = handle;
                ::events::EventDispatcher::instance().publish(notif);
                continue;
            }

            glm::vec3 waypoint = state.path.waypoints[state.currentIndex];
            glm::vec3 direction = waypoint - transform.position;
            float distToWaypoint = glm::length(direction);

            // Advance waypoint when within arrival distance
            if (distToWaypoint <= arrivalDist)
            {
                state.currentIndex++;
                if (state.currentIndex >= state.path.waypoints.size())
                {
                    state.path = volumetric::VolumePath{};
                    state.velocity = glm::vec3(0.0f);

                    events::volumetric::VolumetricAgentReachedDestinationNotification notif;
                    notif.entity = handle;
                    ::events::EventDispatcher::instance().publish(notif);
                    continue;
                }
                waypoint = state.path.waypoints[state.currentIndex];
                direction = waypoint - transform.position;
                distToWaypoint = glm::length(direction);
            }

            // Accelerate toward desired velocity
            glm::vec3 desiredVelocity{0.0f};
            if (distToWaypoint > 0.001f)
            {
                desiredVelocity = (direction / distToWaypoint) * maxSpeed;
            }

            glm::vec3 velocityDiff = desiredVelocity - state.velocity;
            float diffLen = glm::length(velocityDiff);
            if (diffLen > 0.001f)
            {
                float maxAccelStep = maxAcceleration * deltaTime;
                if (diffLen <= maxAccelStep)
                {
                    state.velocity = desiredVelocity;
                }
                else
                {
                    state.velocity += (velocityDiff / diffLen) * maxAccelStep;
                }
            }

            // Update position
            transform.position += state.velocity * deltaTime;
            transform.isDirty = true;
        }
    }

    void VolumetricAgentManager::clear()
    {
        agents.clear();
    }
}
