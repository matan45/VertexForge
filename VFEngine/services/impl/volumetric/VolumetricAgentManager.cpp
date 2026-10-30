#include "VolumetricAgentManager.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/scene/EntityTransformEvents.hpp"
#include "../../events/volumetric/VolumetricNavEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "scene/HierarchyMatrix.hpp"
#include "components/Components.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cmath>
#include <exception>
#include <utility>

namespace services
{
    namespace
    {
        // The signed angle (degrees) that turns `from` onto `to`, in [-180, 180).
        float angleDelta(float from, float to)
        {
            float delta = std::fmod(to - from + 180.0f, 360.0f);
            if (delta < 0.0f)
                delta += 360.0f;
            return delta - 180.0f;
        }

        // Turns `angle` toward `target` by at most turnSpeed * deltaTime degrees; turnSpeed <= 0 turns at once.
        float turnToward(float angle, float target, float turnSpeed, float deltaTime)
        {
            const float delta = angleDelta(angle, target);
            if (turnSpeed <= 0.0f)
                return angle + delta;
            const float maxStep = turnSpeed * deltaTime;
            return angle + std::clamp(delta, -maxStep, maxStep);
        }

        // A moving agent must render as dynamic: a static mesh keeps its baked culling bounds and cached shadow
        // while it flies away (as the navmesh agents do).
        void markDynamic(EntityHandle entity)
        {
            auto& registry = scene::EntityRegistry::getRegistry();
            const entt::entity e = internal::fromHandle(entity);
            if (!registry.valid(e))
                return;
            const auto* transform = registry.try_get<components::TransformComponent>(e);
            if (!transform || !transform->isStatic)
                return;

            ::events::scene::SetEntityStaticCommand staticCmd;
            staticCmd.entity = entity;
            staticCmd.isStatic = false;
            try
            {
                ::events::EventDispatcher::instance().execute(staticCmd);
            }
            catch (const std::exception& error)
            {
                vfLogDebug("VolumetricAgentManager: could not mark agent {} dynamic: {}", entity.id, error.what());
            }
        }
    }

    VolumetricAgentManager::VolumetricAgentManager(IVolumetricNavProvider* provider)
        : provider(provider)
    {
    }

    bool VolumetricAgentManager::setDestination(EntityHandle entity, const glm::vec3& target)
    {
        std::vector<Event> events;
        bool registered = false;
        {
            std::lock_guard lock(mutex);
            auto& registry = scene::EntityRegistry::getRegistry();
            if (!internal::isValidHandle(entity, registry))
                return false;

            const entt::entity e = internal::fromHandle(entity);
            auto* component = registry.try_get<components::VolumetricAgentComponent>(e);
            if (!component || !registry.all_of<components::TransformComponent>(e))
                return false;

            auto [it, inserted] = agents.try_emplace(entity.id);
            registered = inserted;
            component->isActive = true;

            // A new move: nothing of the previous one (Arrived / Failed, its path) survives. The velocity does,
            // so re-targeting a moving agent (a chase) stays smooth.
            Agent& agent = it->second;
            agent.state = VolumetricAgentState{};
            agent.state.destination = target;
            plan(entity, agent, scene::composeWorldPosition(registry, e), component->stoppingDistance, events);
        }

        if (registered)
            markDynamic(entity);
        publish(events);
        return true;
    }

    void VolumetricAgentManager::stopAgent(EntityHandle entity)
    {
        std::lock_guard lock(mutex);
        auto it = agents.find(entity.id);
        if (it == agents.end())
            return;

        Agent& agent = it->second;
        agent.state.state = VolumetricAgentMoveState::Idle;
        agent.state.failReason.clear();
        agent.state.partial = false;
        agent.state.remaining = 0.0f;
        agent.waypoints.clear();
        agent.nextWaypoint = 0;
        agent.velocity = glm::vec3(0.0f);
        agent.grid.reset();
    }

    void VolumetricAgentManager::removeAgent(EntityHandle entity)
    {
        std::lock_guard lock(mutex);
        if (agents.erase(entity.id) == 0)
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        const entt::entity e = internal::fromHandle(entity);
        if (registry.valid(e))
        {
            if (auto* component = registry.try_get<components::VolumetricAgentComponent>(e))
                component->isActive = false;
        }
    }

    void VolumetricAgentManager::clear()
    {
        std::lock_guard lock(mutex);
        auto& registry = scene::EntityRegistry::getRegistry();
        for (const auto& [id, agent] : agents)
        {
            const entt::entity e = internal::fromHandle(EntityHandle{id});
            if (!registry.valid(e))
                continue;
            if (auto* component = registry.try_get<components::VolumetricAgentComponent>(e))
                component->isActive = false;
        }
        agents.clear();
    }

    void VolumetricAgentManager::update(float deltaTime)
    {
        std::vector<Event> events;
        {
            std::lock_guard lock(mutex);
            auto& registry = scene::EntityRegistry::getRegistry();
            for (auto it = agents.begin(); it != agents.end();)
            {
                const EntityHandle handle{it->first};
                const entt::entity entity = internal::fromHandle(handle);
                if (!registry.valid(entity) ||
                    !registry.all_of<components::VolumetricAgentComponent, components::TransformComponent>(entity))
                {
                    // The entity or its agent component is gone: forget it.
                    it = agents.erase(it);
                    continue;
                }

                Agent& agent = it->second;
                if (agent.state.state != VolumetricAgentMoveState::Moving)
                {
                    ++it;
                    continue;
                }

                const auto& settings = registry.get<components::VolumetricAgentComponent>(entity);
                const float stopDistance = std::max(settings.stoppingDistance, 0.0f);
                glm::vec3 position = scene::composeWorldPosition(registry, entity);

                // The volume under the path was re-baked, reloaded or removed: plan again on what is there now.
                if (provider->getVolume(agent.state.volumeId) != agent.grid &&
                    !plan(handle, agent, position, stopDistance, events))
                {
                    ++it;
                    continue;
                }

                // Pass the waypoints already reached; the last one is the destination (or the partial end).
                const float reach = std::max(agent.grid ? agent.grid->voxelSize() * 0.5f : 0.25f, 0.05f);
                while (agent.nextWaypoint + 1 < agent.waypoints.size() &&
                       glm::distance(position, agent.waypoints[agent.nextWaypoint]) <= reach)
                {
                    ++agent.nextWaypoint;
                }

                const glm::vec3 goal = agent.waypoints.back();
                const bool finalLeg = agent.nextWaypoint + 1 >= agent.waypoints.size();
                const float toGoal = glm::distance(position, goal);

                if (finalLeg && toGoal <= std::max(stopDistance, 1.0e-3f))
                {
                    if (agent.state.partial)
                    {
                        // The end of a partial path: carry on toward the real destination (NoPath fails the move
                        // once no further progress is possible).
                        plan(handle, agent, position, stopDistance, events);
                        ++it;
                        continue;
                    }

                    agent.velocity = glm::vec3(0.0f);
                    agent.state.state = VolumetricAgentMoveState::Arrived;
                    agent.state.remaining = 0.0f;
                    agent.waypoints.clear();
                    agent.nextWaypoint = 0;
                    events.push_back(Event{handle, true, {}});
                    ++it;
                    continue;
                }

                if (deltaTime > 0.0f)
                {
                    // Arrive steering: full speed along the path, braking on the last leg so the agent stops
                    // within stoppingDistance of the end. maxAcceleration <= 0 changes velocity at once.
                    const glm::vec3 target = agent.waypoints[agent.nextWaypoint];
                    const glm::vec3 toTarget = target - position;
                    const float targetDistance = glm::length(toTarget);
                    const glm::vec3 direction = targetDistance > 1.0e-5f ? toTarget / targetDistance : glm::vec3(0.0f);

                    float desiredSpeed = std::max(settings.maxSpeed, 0.0f);
                    if (finalLeg && !agent.state.partial && settings.maxAcceleration > 0.0f)
                    {
                        const float brakingDistance = std::max(toGoal - stopDistance, 0.0f);
                        desiredSpeed = std::min(desiredSpeed, std::sqrt(2.0f * settings.maxAcceleration * brakingDistance));
                    }

                    const glm::vec3 desiredVelocity = direction * desiredSpeed;
                    if (settings.maxAcceleration > 0.0f)
                    {
                        glm::vec3 change = desiredVelocity - agent.velocity;
                        const float changeLength = glm::length(change);
                        const float maxChange = settings.maxAcceleration * deltaTime;
                        if (changeLength > maxChange && changeLength > 0.0f)
                            change *= maxChange / changeLength;
                        agent.velocity += change;
                    }
                    else
                    {
                        agent.velocity = desiredVelocity;
                    }

                    glm::vec3 step = agent.velocity * deltaTime;
                    const float stepLength = glm::length(step);
                    if (finalLeg && stepLength >= toGoal && stepLength > 0.0f)
                        step *= toGoal / stepLength; // never fly past the end
                    position += step;

                    // World -> local: the transform is relative to the parent.
                    auto& transform = registry.get<components::TransformComponent>(entity);
                    glm::vec3 local = position;
                    if (const auto* parent = registry.try_get<components::ParentComponent>(entity);
                        parent && parent->parent != entt::null && registry.valid(parent->parent))
                    {
                        const glm::mat4 parentWorld = scene::composeWorldMatrix(registry, parent->parent);
                        local = glm::vec3(glm::inverse(parentWorld) * glm::vec4(position, 1.0f));
                    }
                    transform.position = local;

                    // Face the travel direction: yaw (rotation.y, 0 = +Z) and, with alignPitch, pitch (rotation.x;
                    // a positive angle tips +Z down). The angles are the local rotation (an unrotated parent).
                    const float horizontalSpeed = std::sqrt(agent.velocity.x * agent.velocity.x +
                                                            agent.velocity.z * agent.velocity.z);
                    if (horizontalSpeed > 1.0e-3f)
                    {
                        const float targetYaw = glm::degrees(std::atan2(agent.velocity.x, agent.velocity.z));
                        transform.rotation.y = turnToward(transform.rotation.y, targetYaw, settings.turnSpeed, deltaTime);
                    }
                    if (settings.alignPitch && glm::length(agent.velocity) > 1.0e-3f)
                    {
                        const float targetPitch = -glm::degrees(std::atan2(agent.velocity.y, horizontalSpeed));
                        transform.rotation.x = turnToward(transform.rotation.x, targetPitch, settings.turnSpeed, deltaTime);
                    }
                    transform.isDirty = true;
                }

                // No progress along the path for STUCK_SECONDS: the move fails.
                agent.state.remaining = remainingLength(agent, position);
                if (agent.state.remaining < agent.progressMark - STUCK_PROGRESS)
                {
                    agent.progressMark = agent.state.remaining;
                    agent.stuckTimer = 0.0f;
                }
                else
                {
                    agent.stuckTimer += deltaTime;
                    if (agent.stuckTimer >= STUCK_SECONDS)
                        fail(handle, agent, "Stuck", events);
                }
                ++it;
            }
        }
        publish(events);
    }

    std::optional<VolumetricAgentState> VolumetricAgentManager::getState(EntityHandle entity) const
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!internal::isValidHandle(entity, registry) ||
            !registry.all_of<components::VolumetricAgentComponent>(internal::fromHandle(entity)))
        {
            return std::nullopt;
        }

        std::lock_guard lock(mutex);
        auto it = agents.find(entity.id);
        if (it == agents.end())
            return VolumetricAgentState{};
        return it->second.state;
    }

    bool VolumetricAgentManager::isRegistered(EntityHandle entity) const
    {
        std::lock_guard lock(mutex);
        return agents.contains(entity.id);
    }

    std::vector<std::vector<glm::vec3>> VolumetricAgentManager::activePaths() const
    {
        std::vector<std::vector<glm::vec3>> paths;
        std::lock_guard lock(mutex);
        auto& registry = scene::EntityRegistry::getRegistry();
        for (const auto& [id, agent] : agents)
        {
            if (agent.state.state != VolumetricAgentMoveState::Moving || agent.waypoints.empty())
                continue;
            const entt::entity e = internal::fromHandle(EntityHandle{id});
            if (!registry.valid(e))
                continue;

            std::vector<glm::vec3> path;
            path.reserve(agent.waypoints.size() - agent.nextWaypoint + 1);
            path.push_back(scene::composeWorldPosition(registry, e));
            for (size_t i = agent.nextWaypoint; i < agent.waypoints.size(); ++i)
                path.push_back(agent.waypoints[i]);
            paths.push_back(std::move(path));
        }
        return paths;
    }

    bool VolumetricAgentManager::plan(EntityHandle entity, Agent& agent, const glm::vec3& from, float stopDistance,
                                      std::vector<Event>& events)
    {
        volumetric::PathRequest request;
        request.start = from;
        request.end = agent.state.destination;
        request.snapRadius = PATH_SNAP_RADIUS;
        request.maxNodes = PATH_MAX_NODES;
        const volumetric::VolumePathResult result = provider->findPath3D(request);

        agent.waypoints.clear();
        agent.nextWaypoint = 0;
        agent.stuckTimer = 0.0f;
        agent.state.volumeId = result.volumeId;
        if (!result.hasPath() || result.waypoints.empty())
        {
            fail(entity, agent, volumetric::volumePathStatusName(result.status), events);
            return false;
        }

        const bool partial = result.status == volumetric::VolumePathStatus::Partial;
        if (partial && glm::distance(from, result.waypoints.back()) <= std::max(stopDistance, 1.0e-3f))
        {
            // Already as close as the volume allows: walking the partial path would not get any nearer.
            fail(entity, agent, volumetric::volumePathStatusName(volumetric::VolumePathStatus::NoPath), events);
            return false;
        }

        // The first waypoint is the (possibly snapped) start; update() passes it once it is reached.
        agent.waypoints = result.waypoints;
        agent.grid = provider->getVolume(result.volumeId);
        agent.state.state = VolumetricAgentMoveState::Moving;
        agent.state.failReason.clear();
        agent.state.partial = partial;
        agent.state.remaining = remainingLength(agent, from);
        agent.progressMark = agent.state.remaining;
        return true;
    }

    void VolumetricAgentManager::fail(EntityHandle entity, Agent& agent, std::string reason, std::vector<Event>& events)
    {
        agent.state.state = VolumetricAgentMoveState::Failed;
        agent.state.failReason = reason;
        agent.state.remaining = 0.0f;
        agent.waypoints.clear();
        agent.nextWaypoint = 0;
        agent.velocity = glm::vec3(0.0f);
        agent.grid.reset();
        events.push_back(Event{entity, false, std::move(reason)});
    }

    float VolumetricAgentManager::remainingLength(const Agent& agent, const glm::vec3& position)
    {
        if (agent.waypoints.empty() || agent.nextWaypoint >= agent.waypoints.size())
            return 0.0f;
        float length = glm::distance(position, agent.waypoints[agent.nextWaypoint]);
        for (size_t i = agent.nextWaypoint; i + 1 < agent.waypoints.size(); ++i)
            length += glm::distance(agent.waypoints[i], agent.waypoints[i + 1]);
        return length;
    }

    void VolumetricAgentManager::publish(const std::vector<Event>& events)
    {
        if (events.empty())
            return;
        auto& dispatcher = ::events::EventDispatcher::instance();
        for (const Event& event : events)
        {
            if (event.reached)
            {
                ::events::volumetric::VolumetricAgentReachedDestinationNotification notification;
                notification.entity = event.entity;
                dispatcher.publish(notification);
            }
            else
            {
                ::events::volumetric::VolumetricAgentPathFailedNotification notification;
                notification.entity = event.entity;
                notification.reason = event.reason;
                dispatcher.publish(notification);
            }
        }
    }
}
