#include "NavmeshAgentManager.hpp"
#include "../../data/EntityConversion.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/Components.hpp"
#include "navigation/CorridorFollow.hpp"
#include "navigation/RootMotionSpeed.hpp"
#include "print/Log.hpp"
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace services
{
    NavmeshAgentManager::NavmeshAgentManager(INavmeshProvider* provider, TileCoordFunc worldToTileCoordFunc,
                                             TileVersionFunc tileVersionFunc)
        : navmeshProvider(provider),
          worldToTileCoord(std::move(worldToTileCoordFunc)),
          tileVersion(std::move(tileVersionFunc))
    {
    }

    bool NavmeshAgentManager::resolveDestination(const glm::vec3& target, glm::vec3& snapped) const
    {
        snapped = target;
        if (navmeshProvider->isPointOnNavmesh(target, SNAP_ON_MESH_TOLERANCE))
            return true;

        for (float r : SNAP_SEARCH_RADII)
        {
            glm::vec3 candidate = navmeshProvider->getClosestPoint(target, r);
            if (navmeshProvider->isPointOnNavmesh(candidate, SNAP_ON_MESH_TOLERANCE))
            {
                snapped = candidate;
                return true;
            }
        }

        return false;
    }

    int NavmeshAgentManager::ensureAgentRegistered(EntityHandle entity)
    {
        auto it = entityToAgentIndex.find(entity.id);
        if (it != entityToAgentIndex.end())
            return it->second;

        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = internal::fromHandle(entity);
        if (!registry.valid(enttEntity) ||
            !registry.all_of<components::NavmeshAgentComponent>(enttEntity))
        {
            return -1;
        }

        addAgent(entity);
        it = entityToAgentIndex.find(entity.id);
        if (it == entityToAgentIndex.end())
            return -1;

        return it->second;
    }

    void NavmeshAgentManager::addAgent(EntityHandle entity)
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

    void NavmeshAgentManager::removeAgent(EntityHandle entity)
    {
        removeEntityFromGroup(entity.id);

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
            entityToTarget.erase(entity.id);
            entityStuckTimer.erase(entity.id);
            velocityTransitionFrames.erase(entity.id);
        }
    }

    void NavmeshAgentManager::setAgentDestination(EntityHandle entity, const glm::vec3& target)
    {
        // Snap the requested destination onto the navmesh. A raw off-mesh point would
        // otherwise leave the crowd agent with no valid target (setCrowdAgentTarget's
        // findNearestPoly finds nothing) and trip false stuck-detection in updatePositions.
        // getClosestPoint returns the input unchanged when nothing is found, so each
        // candidate is validated with isPointOnNavmesh and the search widens on failure.
        glm::vec3 snapped;
        if (!resolveDestination(target, snapped))
        {
            // No navmesh poly within the widest search radius (or the navmesh isn't loaded
            // yet): clear any prior target so the agent idles cleanly instead of accumulating
            // a false "blocked" stuck timer. Logged so a dropped move order is diagnosable
            // rather than a silent freeze.
            vfLogWarning("NavmeshAgentManager: destination ({:.1f}, {:.1f}, {:.1f}) is more than "
                         "{:.0f} units off the navmesh (or navmesh not loaded) - move order dropped",
                         target.x, target.y, target.z, SNAP_SEARCH_RADII[std::size(SNAP_SEARCH_RADII) - 1]);
            stopAgent(entity);
            return;
        }

        // A direct per-agent destination supersedes any generic group steering.
        removeEntityFromGroup(entity.id);

        int agentIdx = ensureAgentRegistered(entity);
        if (agentIdx < 0)
            return;  // off-navmesh / crowd full: addCrowdAgent failed

        navmeshProvider->setCrowdAgentTarget(agentIdx, snapped);
        entityToTarget[entity.id] = snapped;
        entityStuckTimer.erase(entity.id);
    }

    void NavmeshAgentManager::removeEntityFromGroup(uint64_t entityId)
    {
        auto groupIt = entityToGroup.find(entityId);
        if (groupIt == entityToGroup.end())
            return;

        const uint64_t groupId = groupIt->second;
        entityToGroup.erase(groupIt);

        auto it = groups.find(groupId);
        if (it == groups.end())
            return;

        auto& members = it->second.members;
        members.erase(std::remove_if(members.begin(), members.end(),
            [entityId](const GroupMember& member)
            {
                return member.entityId == entityId;
            }), members.end());

        if (members.empty())
            groups.erase(it);
    }

    uint64_t NavmeshAgentManager::setGroupDestination(const std::vector<EntityHandle>& entities,
                                                      const glm::vec3& target,
                                                      const navigation::FormationParams& formation)
    {
        if (entities.empty())
            return 0;

        navigation::FormationParams params = formation;
        params.spacing = std::max(0.1f, params.spacing);

        glm::vec3 snapped;
        if (!resolveDestination(target, snapped))
        {
            vfLogWarning("NavmeshAgentManager: group destination ({:.1f}, {:.1f}, {:.1f}) is more than "
                         "{:.0f} units off the navmesh (or navmesh not loaded) - group move dropped",
                         target.x, target.y, target.z, SNAP_SEARCH_RADII[std::size(SNAP_SEARCH_RADII) - 1]);
            for (const EntityHandle& entity : entities)
                stopAgent(entity);
            return 0;
        }

        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<EntityHandle> validEntities;
        std::vector<glm::vec3> positions;
        validEntities.reserve(entities.size());
        positions.reserve(entities.size());

        glm::vec3 centroid{0.0f};
        for (const EntityHandle& entity : entities)
        {
            auto enttEntity = internal::fromHandle(entity);
            if (!registry.valid(enttEntity) ||
                !registry.all_of<components::TransformComponent, components::NavmeshAgentComponent>(enttEntity))
            {
                continue;
            }

            int agentIdx = ensureAgentRegistered(entity);
            if (agentIdx < 0)
                continue;

            const glm::vec3 position = registry.get<components::TransformComponent>(enttEntity).position;
            validEntities.push_back(entity);
            positions.push_back(position);
            centroid += position;
        }

        if (validEntities.empty())
            return 0;

        centroid /= static_cast<float>(validEntities.size());

        glm::vec3 start = centroid;
        if (!navmeshProvider->isPointOnNavmesh(start, SNAP_ON_MESH_TOLERANCE))
        {
            for (float r : SNAP_SEARCH_RADII)
            {
                glm::vec3 candidate = navmeshProvider->getClosestPoint(start, r);
                if (navmeshProvider->isPointOnNavmesh(candidate, SNAP_ON_MESH_TOLERANCE))
                {
                    start = candidate;
                    break;
                }
            }
        }

        glm::vec3 toDestination = snapped - centroid;
        if (toDestination.x * toDestination.x + toDestination.z * toDestination.z < 1e-6f)
            toDestination = glm::vec3(0.0f, 0.0f, 1.0f);
        const float facingYaw = glm::atan(toDestination.x, toDestination.z);

        std::vector<int> slotForUnit(validEntities.size(), 0);
        navigation::assignSlotsStable(positions, centroid, facingYaw, params, slotForUnit);

        GroupCorridor group;
        group.destination = snapped;
        group.formation = params;
        group.builtTileVersion = tileVersion ? tileVersion() : 0;
        group.corridor = navmeshProvider->findPath(start, snapped, 0.25f, 2.0f);
        if (!group.corridor.isValid || group.corridor.waypoints.empty())
        {
            group.corridor.isValid = true;
            group.corridor.waypoints = {start, snapped};
        }

        const uint64_t groupId = nextGroupId++;
        group.members.reserve(validEntities.size());

        for (size_t i = 0; i < validEntities.size(); ++i)
        {
            const EntityHandle entity = validEntities[i];
            removeEntityFromGroup(entity.id);

            const int slot = slotForUnit[i];
            const glm::vec3 slotLocal = navigation::formationSlotLocal(
                params.kind, slot, static_cast<int>(validEntities.size()), params.spacing);
            glm::vec3 slotWorld = navigation::toWorld(slotLocal, snapped, facingYaw);
            slotWorld.y = snapped.y;
            if (!navmeshProvider->isPointOnNavmesh(slotWorld, SNAP_ON_MESH_TOLERANCE))
            {
                glm::vec3 candidate = navmeshProvider->getClosestPoint(slotWorld, params.spacing * 2.0f + 1.0f);
                if (navmeshProvider->isPointOnNavmesh(candidate, SNAP_ON_MESH_TOLERANCE))
                    slotWorld = candidate;
            }

            entityToGroup[entity.id] = groupId;
            entityToTarget[entity.id] = slotWorld;
            entityStuckTimer.erase(entity.id);
            velocityTransitionFrames.erase(entity.id);

            GroupMember member;
            member.entityId = entity.id;
            member.slot = slot;
            member.lateralOffset = slotLocal.x;
            member.slotTarget = slotWorld;
            group.members.push_back(member);
        }

        groups[groupId] = std::move(group);
        return groupId;
    }

    void NavmeshAgentManager::stopAgent(EntityHandle entity)
    {
        removeEntityFromGroup(entity.id);

        auto it = entityToAgentIndex.find(entity.id);
        if (it != entityToAgentIndex.end())
        {
            navmeshProvider->stopCrowdAgent(it->second);
            entityToTarget.erase(entity.id);
            entityStuckTimer.erase(entity.id);
            velocityTransitionFrames.erase(entity.id);
        }
    }

    void NavmeshAgentManager::updateAgentConfig(EntityHandle entity, float maxSpeed, float maxAcceleration,
                                                int rootMotionDriven, float rootMotionSpeedScale, float turnSpeed)
    {
        // Persist onto the component first, so values set before the agent joins the
        // crowd (lazy registration on first setAgentDestination) survive and are read
        // by addAgent when it finally registers.
        auto& registry = scene::EntityRegistry::getRegistry();
        auto enttEntity = internal::fromHandle(entity);
        bool disabledRootMotion = false;
        float configuredSpeed = maxSpeed;
        if (registry.valid(enttEntity) && registry.all_of<components::NavmeshAgentComponent>(enttEntity))
        {
            auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);
            if (maxSpeed >= 0.0f) agent.maxSpeed = maxSpeed;
            if (maxAcceleration >= 0.0f) agent.maxAcceleration = maxAcceleration;
            if (rootMotionDriven >= 0)
            {
                const bool enable = (rootMotionDriven != 0);
                disabledRootMotion = agent.rootMotionDriven && !enable;
                agent.rootMotionDriven = enable;
            }
            if (rootMotionSpeedScale >= 0.0f) agent.rootMotionSpeedScale = rootMotionSpeedScale;
            if (turnSpeed >= 0.0f) agent.turnSpeed = turnSpeed;
            configuredSpeed = agent.maxSpeed;
        }

        auto it = entityToAgentIndex.find(entity.id);
        if (it == entityToAgentIndex.end()) return;

        navmeshProvider->updateCrowdAgentParams(it->second, maxSpeed, maxAcceleration);

        // When disabling root-motion pacing, restore the crowd to the configured maxSpeed.
        if (disabledRootMotion)
            navmeshProvider->updateCrowdAgentParams(it->second, configuredSpeed, -1.0f);
    }

    glm::vec3 NavmeshAgentManager::getAgentVelocity(EntityHandle entity) const
    {
        auto it = entityToAgentIndex.find(entity.id);
        if (it == entityToAgentIndex.end()) return glm::vec3(0.0f);
        return navmeshProvider->getCrowdAgentVelocity(it->second);
    }

    float NavmeshAgentManager::getAgentSpeed(EntityHandle entity) const
    {
        auto it = entityToAgentIndex.find(entity.id);
        if (it == entityToAgentIndex.end()) return 0.0f;
        return navmeshProvider->getCrowdAgentMaxSpeed(it->second);
    }

    NavmeshAgentManager::GroupMoveStatus NavmeshAgentManager::getGroupStatus(uint64_t groupId) const
    {
        GroupMoveStatus status;
        auto it = groups.find(groupId);
        if (it == groups.end())
        {
            status.complete = groupId != 0;
            return status;
        }

        status.total = static_cast<int>(it->second.members.size());
        for (const GroupMember& member : it->second.members)
        {
            if (member.arrived)
            {
                ++status.arrived;
                continue;
            }

            auto agentIt = entityToAgentIndex.find(member.entityId);
            if (agentIt == entityToAgentIndex.end())
                continue;

            const glm::vec3 pos = navmeshProvider->getCrowdAgentPosition(agentIt->second);
            if (glm::distance(pos, member.slotTarget) <= ARRIVAL_DISTANCE)
                ++status.arrived;
        }

        status.complete = status.total > 0 && status.arrived >= status.total;
        return status;
    }

    std::vector<glm::vec3> NavmeshAgentManager::getGroupCorridor(uint64_t groupId) const
    {
        auto it = groups.find(groupId);
        if (it == groups.end())
            return {};
        return it->second.corridor.waypoints;
    }

    bool NavmeshAgentManager::rebuildGroupCorridor(GroupCorridor& group, uint64_t currentVersion)
    {
        glm::vec3 centroid{0.0f};
        int count = 0;
        for (const GroupMember& member : group.members)
        {
            if (member.arrived)
                continue;

            auto agentIt = entityToAgentIndex.find(member.entityId);
            if (agentIt == entityToAgentIndex.end())
                continue;

            centroid += navmeshProvider->getCrowdAgentPosition(agentIt->second);
            ++count;
        }

        if (count <= 0)
        {
            group.builtTileVersion = currentVersion;
            return false;
        }

        centroid /= static_cast<float>(count);
        glm::vec3 start = centroid;
        if (!navmeshProvider->isPointOnNavmesh(start, SNAP_ON_MESH_TOLERANCE))
        {
            for (float r : SNAP_SEARCH_RADII)
            {
                glm::vec3 candidate = navmeshProvider->getClosestPoint(start, r);
                if (navmeshProvider->isPointOnNavmesh(candidate, SNAP_ON_MESH_TOLERANCE))
                {
                    start = candidate;
                    break;
                }
            }
        }

        navigation::NavPath path = navmeshProvider->findPath(start, group.destination, 0.25f, 2.0f);
        if (!path.isValid || path.waypoints.empty())
        {
            path.isValid = true;
            path.waypoints = {start, group.destination};
        }

        group.corridor = std::move(path);
        group.builtTileVersion = currentVersion;
        return true;
    }

    void NavmeshAgentManager::updateGroupSteering(float)
    {
        if (groups.empty())
            return;

        const uint64_t currentVersion = tileVersion ? tileVersion() : 0;
        int repaths = 0;

        for (auto& groupEntry : groups)
        {
            GroupCorridor& group = groupEntry.second;
            if (navigation::corridorStale(group.builtTileVersion, currentVersion) &&
                repaths < MAX_GROUP_REPATHS_PER_FRAME)
            {
                rebuildGroupCorridor(group, currentVersion);
                ++repaths;
            }

            const float totalLength = navigation::corridorTotalLength(group.corridor);
            const float finalApproachRadius = std::max(group.formation.spacing * 2.0f, 4.0f);
            for (GroupMember& member : group.members)
            {
                if (member.arrived)
                {
                    holdGroupMemberAtStop(member.entityId);
                    continue;
                }

                auto agentIt = entityToAgentIndex.find(member.entityId);
                if (agentIt == entityToAgentIndex.end())
                    continue;

                const int agentIdx = agentIt->second;
                const glm::vec3 pos = navmeshProvider->getCrowdAgentPosition(agentIdx);
                const float maxSpeed = navmeshProvider->getCrowdAgentMaxSpeed(agentIdx);
                glm::vec3 velocity{0.0f};

                const auto projection = navigation::projectOntoCorridor(group.corridor, pos);
                const float remaining = std::max(0.0f, totalLength - projection.arcLength);
                const glm::vec3 toSlot = member.slotTarget - pos;
                const glm::vec3 planarToSlot{toSlot.x, 0.0f, toSlot.z};
                const float slotDist = glm::length(planarToSlot);

                if (slotDist <= ARRIVAL_DISTANCE)
                {
                    member.arrived = true;
                    velocity = glm::vec3(0.0f);
                }
                else if (remaining <= finalApproachRadius)
                {
                    const float slowRadius = std::max(ARRIVAL_DISTANCE * 3.0f, 1.0f);
                    const float scale = std::clamp(slotDist / slowRadius, 0.2f, 1.0f);
                    velocity = planarToSlot / slotDist * maxSpeed * scale;
                }
                else
                {
                    navigation::CorridorFollowParams params;
                    params.lookAhead = std::max(group.formation.spacing, 2.0f);
                    params.arriveRadius = std::max(ARRIVAL_DISTANCE, group.formation.spacing * 0.5f);
                    params.lateralOffset = member.lateralOffset;
                    params.corridorHalfWidth = std::max(group.formation.spacing * 2.0f, 0.25f);
                    velocity = navigation::corridorFollowVelocity(group.corridor, pos, maxSpeed, params);
                }

                navmeshProvider->overrideCrowdAgentVelocity(agentIdx, velocity);
            }
        }
    }

    void NavmeshAgentManager::holdGroupMemberAtStop(uint64_t entityId)
    {
        auto agentIt = entityToAgentIndex.find(entityId);
        if (agentIt == entityToAgentIndex.end())
            return;

        // Group steering drives Detour with requestMoveVelocity(). Once a member
        // reaches its slot, keep submitting a zero velocity target until the group
        // completes or a new order replaces it; otherwise the agent may keep the
        // previous velocity request and drift through the destination.
        navmeshProvider->overrideCrowdAgentVelocity(agentIt->second, glm::vec3(0.0f));
    }

    void NavmeshAgentManager::markGroupMemberArrived(uint64_t entityId)
    {
        auto groupIt = entityToGroup.find(entityId);
        if (groupIt == entityToGroup.end())
            return;

        auto it = groups.find(groupIt->second);
        if (it == groups.end())
            return;

        for (GroupMember& member : it->second.members)
        {
            if (member.entityId == entityId)
            {
                member.arrived = true;
                holdGroupMemberAtStop(entityId);
                break;
            }
        }
    }

    void NavmeshAgentManager::markGroupNeedsRepath(uint64_t entityId)
    {
        auto groupIt = entityToGroup.find(entityId);
        if (groupIt == entityToGroup.end())
            return;

        auto it = groups.find(groupIt->second);
        if (it != groups.end())
            it->second.builtTileVersion = UINT64_MAX;
    }

    void NavmeshAgentManager::finishCompletedGroups()
    {
        if (groups.empty())
            return;

        auto& registry = scene::EntityRegistry::getRegistry();
        std::vector<uint64_t> completed;

        for (auto& [groupId, group] : groups)
        {
            bool allArrived = !group.members.empty();
            for (GroupMember& member : group.members)
            {
                if (!member.arrived)
                {
                    auto agentIt = entityToAgentIndex.find(member.entityId);
                    if (agentIt != entityToAgentIndex.end())
                    {
                        const glm::vec3 pos = navmeshProvider->getCrowdAgentPosition(agentIt->second);
                        if (glm::distance(pos, member.slotTarget) <= ARRIVAL_DISTANCE)
                        {
                            member.arrived = true;
                            holdGroupMemberAtStop(member.entityId);
                        }
                    }
                }

                allArrived = allArrived && member.arrived;
            }

            if (allArrived)
            {
                events::navmesh::GroupReachedDestinationNotification notif;
                notif.groupId = groupId;
                for (const GroupMember& member : group.members)
                {
                    EntityHandle handle{member.entityId};
                    auto enttEntity = internal::fromHandle(handle);
                    if (registry.valid(enttEntity))
                        notif.entities.push_back(handle);
                }
                ::events::EventDispatcher::instance().publish(notif);
                completed.push_back(groupId);
            }
        }

        for (uint64_t groupId : completed)
        {
            auto it = groups.find(groupId);
            if (it == groups.end())
                continue;

            for (const GroupMember& member : it->second.members)
            {
                entityToGroup.erase(member.entityId);
                entityToTarget.erase(member.entityId);
                entityStuckTimer.erase(member.entityId);
            }
            groups.erase(it);
        }
    }

    void NavmeshAgentManager::updatePositions(float deltaTime)
    {
        if (entityToAgentIndex.empty())
        {
            return;
        }

        // VK-1408: for root-motion-driven agents, set the crowd maxSpeed from the
        // animation's per-frame ground speed BEFORE updateCrowd, so the clip paces the
        // unit (no double-move, no foot slide). When the animator was frustum-culled
        // this frame (no fresh sample), fall back to the configured maxSpeed.
        {
            auto& rmRegistry = scene::EntityRegistry::getRegistry();
            for (const auto& [entityId, agentIdx] : entityToAgentIndex)
            {
                auto enttEntity = internal::fromHandle(EntityHandle{entityId});
                if (!rmRegistry.valid(enttEntity) ||
                    !rmRegistry.all_of<components::NavmeshAgentComponent>(enttEntity))
                    continue;
                auto& agent = rmRegistry.get<components::NavmeshAgentComponent>(enttEntity);
                if (!agent.rootMotionDriven)
                    continue;

                float spd;
                if (agent.rootMotionFresh)
                {
                    spd = navigation::computeRootMotionCrowdSpeed(
                        agent.rootMotionPlanarDistance, deltaTime,
                        agent.rootMotionSpeedSmoothed, agent.maxSpeed);
                    agent.rootMotionFresh = false;
                }
                else
                {
                    spd = agent.maxSpeed;
                    agent.rootMotionSpeedSmoothed = agent.maxSpeed;
                }
                navmeshProvider->updateCrowdAgentParams(agentIdx, spd, -1.0f);
            }
        }

        updateGroupSteering(deltaTime);

        navmeshProvider->updateCrowd(deltaTime);

        // Process velocity transitions for recently-resumed agents
        if (!velocityTransitionFrames.empty())
        {
            auto transIt = velocityTransitionFrames.begin();
            while (transIt != velocityTransitionFrames.end())
            {
                transIt->second--;
                if (transIt->second <= 0)
                {
                    auto agentIt = entityToAgentIndex.find(transIt->first);
                    auto targetIt = entityToTarget.find(transIt->first);
                    if (agentIt != entityToAgentIndex.end() && targetIt != entityToTarget.end() &&
                        entityToGroup.find(transIt->first) == entityToGroup.end())
                    {
                        navmeshProvider->setCrowdAgentTarget(agentIt->second, targetIt->second);
                    }
                    transIt = velocityTransitionFrames.erase(transIt);
                }
                else
                {
                    ++transIt;
                }
            }
        }

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

            // Face the travel direction (yaw about Y) when actually moving, so the
            // mesh turns toward its path instead of sliding sideways. rotation is
            // euler degrees; forward is +Z (flip +180 in content if a mesh faces -Z).
            glm::vec3 agentVel = navmeshProvider->getCrowdAgentVelocity(agentIdx);
            if (agentVel.x * agentVel.x + agentVel.z * agentVel.z > 1e-4f)
            {
                const float targetYaw = glm::degrees(glm::atan(agentVel.x, agentVel.z));
                float turnSpeed = 0.0f;
                if (registry.all_of<components::NavmeshAgentComponent>(enttEntity))
                    turnSpeed = registry.get<components::NavmeshAgentComponent>(enttEntity).turnSpeed;

                if (turnSpeed <= 0.0f)
                {
                    transform.rotation.y = targetYaw;  // instant snap (default / back-compat)
                }
                else
                {
                    // Rotate toward the heading by at most turnSpeed*dt, the shortest way
                    // around +-180 deg, so a fast unit eases into the turn instead of
                    // snapping (which reads as a forward "jump").
                    float diff = targetYaw - transform.rotation.y;
                    while (diff > 180.0f) diff -= 360.0f;
                    while (diff < -180.0f) diff += 360.0f;
                    const float maxStep = turnSpeed * deltaTime;
                    transform.rotation.y += glm::clamp(diff, -maxStep, maxStep);
                }
            }

            transform.isDirty = true;

            // Check arrival and blocked state
            auto targetIt = entityToTarget.find(entityId);
            if (targetIt != entityToTarget.end())
            {
                // Read per-agent thresholds from component, fall back to defaults
                float arrivalDist = ARRIVAL_DISTANCE;
                float stuckVelThresh = STUCK_VELOCITY_THRESHOLD;
                float stuckTimeThresh = STUCK_TIME_THRESHOLD;
                if (registry.all_of<components::NavmeshAgentComponent>(enttEntity))
                {
                    const auto& agentComp = registry.get<components::NavmeshAgentComponent>(enttEntity);
                    arrivalDist = agentComp.arrivalDistance;
                    stuckVelThresh = agentComp.stuckVelocityThreshold;
                    stuckTimeThresh = agentComp.stuckTimeThreshold;
                }

                float distToTarget = glm::distance(agentPos, targetIt->second);

                if (distToTarget <= arrivalDist)
                {
                    markGroupMemberArrived(entityId);
                    entityToTarget.erase(targetIt);
                    entityStuckTimer.erase(entityId);

                    events::navmesh::AgentReachedDestinationNotification notif;
                    notif.entity = handle;
                    ::events::EventDispatcher::instance().publish(notif);
                }
                else
                {
                    glm::vec3 velocity = navmeshProvider->getCrowdAgentVelocity(agentIdx);
                    float speed = glm::length(velocity);

                    if (speed < stuckVelThresh)
                    {
                        entityStuckTimer[entityId] += deltaTime;
                        if (entityStuckTimer[entityId] >= stuckTimeThresh)
                        {
                            const bool inGroup = entityToGroup.find(entityId) != entityToGroup.end();
                            if (inGroup)
                                markGroupNeedsRepath(entityId);
                            else
                                entityToTarget.erase(targetIt);
                            entityStuckTimer.erase(entityId);

                            events::navmesh::AgentPathBlockedNotification notif;
                            notif.entity = handle;
                            ::events::EventDispatcher::instance().publish(notif);
                        }
                    }
                    else
                    {
                        entityStuckTimer.erase(entityId);
                    }
                }
            }
        }

        finishCompletedGroups();
    }

    void NavmeshAgentManager::suspendAgentsOnUnloadedTiles(
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
                suspended.velocity = navmeshProvider->getCrowdAgentVelocity(agentIdx);

                auto targetIt = entityToTarget.find(entityId);
                if (targetIt != entityToTarget.end())
                {
                    suspended.target = targetIt->second;
                    suspended.hasTarget = true;
                }
                else
                {
                    suspended.target = glm::vec3(0.0f);
                    suspended.hasTarget = false;
                }

                auto enttEntity = internal::fromHandle(EntityHandle{entityId});
                if (registry.valid(enttEntity) && registry.all_of<components::NavmeshAgentComponent>(enttEntity))
                {
                    auto& agent = registry.get<components::NavmeshAgentComponent>(enttEntity);
                    agent.isSuspended = true;
                    agent.suspendedPosition = agentPos;
                    agent.suspendedTarget = suspended.target;
                    agent.hasSuspendedTarget = suspended.hasTarget;
                }

                navmeshProvider->removeCrowdAgent(agentIdx);
                suspendedAgents.push_back(suspended);
                toRemove.push_back(entityId);
            }
        }

        for (uint64_t id : toRemove)
        {
            entityToAgentIndex.erase(id);
            entityToTarget.erase(id);
            entityStuckTimer.erase(id);
            velocityTransitionFrames.erase(id);
        }

        // Agents standing on still-loaded tiles whose TARGET tile just unloaded
        // would chase an unreachable goal — re-issue the target so Detour
        // re-plans to the nearest reachable point
        for (const auto& [entityId, agentIdx] : entityToAgentIndex)
        {
            auto targetIt = entityToTarget.find(entityId);
            if (targetIt == entityToTarget.end())
                continue;

            if (unloadedSet.count(worldToTileCoord(targetIt->second)) &&
                entityToGroup.find(entityId) == entityToGroup.end())
                navmeshProvider->setCrowdAgentTarget(agentIdx, targetIt->second);
        }
    }

    void NavmeshAgentManager::resumeAgentsOnLoadedTiles(
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
                            if (entityToGroup.find(it->entityId) == entityToGroup.end())
                                navmeshProvider->setCrowdAgentTarget(agentIdx, it->target);
                            entityToTarget[it->entityId] = it->target;
                        }

                        // If agent had meaningful velocity, use velocity override for smooth transition
                        float speed = glm::length(it->velocity);
                        if (speed > 0.01f)
                        {
                            navmeshProvider->overrideCrowdAgentVelocity(agentIdx, it->velocity);
                            velocityTransitionFrames[it->entityId] = VELOCITY_TRANSITION_FRAMES;
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

    void NavmeshAgentManager::clear()
    {
        entityToAgentIndex.clear();
        entityToTarget.clear();
        entityStuckTimer.clear();
        suspendedAgents.clear();
        velocityTransitionFrames.clear();
        groups.clear();
        entityToGroup.clear();
    }
}
