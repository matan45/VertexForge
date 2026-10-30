#pragma once

#include "../../providers/volumetric/IVolumetricNavProvider.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/VolumetricNavData.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace services
{
    // VK-1660: flies VolumetricAgent entities along paths through the loaded voxel volumes (Play only - the
    // service calls update() while gameplay runs). An agent registers on its first destination; it moves in WORLD
    // space with arrive steering (maxAcceleration up to maxSpeed, braking to stop within stoppingDistance), turns
    // toward its travel direction (yaw, plus pitch when alignPitch) and writes its position back as a local one
    // under its parent. A Partial path is walked and replanned at its end; a replaced volume grid replans; no
    // progress for STUCK_SECONDS fails the move ("Stuck"). Thread-safe (destinations arrive from the behavior-tree
    // and script tasks, update() runs in the Volumetric task); notifications are published outside the lock.
    class VolumetricAgentManager
    {
    public:
        explicit VolumetricAgentManager(IVolumetricNavProvider* provider);

        // Plans a path from the agent's world position to `target` right away: the state is Moving (or Failed with
        // the path status) when this returns - never the previous move's Arrived / Failed. Registers the agent on
        // first use (VolumetricAgentComponent::isActive; the entity turns dynamic). False when the entity has no
        // VolumetricAgentComponent / TransformComponent.
        bool setDestination(EntityHandle entity, const glm::vec3& target);

        // Stops where it is (state Idle); the agent stays registered.
        void stopAgent(EntityHandle entity);
        // Forgets the agent (isActive = false when its component still exists).
        void removeAgent(EntityHandle entity);
        // Forgets every agent.
        void clear();

        void update(float deltaTime);

        // nullopt when the entity has no VolumetricAgentComponent; Idle when it is not registered.
        std::optional<VolumetricAgentState> getState(EntityHandle entity) const;
        bool isRegistered(EntityHandle entity) const;
        // The rest of each moving agent's path (its position first), for the debug overlay.
        std::vector<std::vector<glm::vec3>> activePaths() const;

        static constexpr float STUCK_SECONDS = 1.5f;     // no progress for this long while Moving fails the move
        static constexpr float STUCK_PROGRESS = 0.05f;   // metres of remaining path that count as progress
        static constexpr float PATH_SNAP_RADIUS = 2.0f;  // a blocked start / end snaps this far (PathRequest)
        static constexpr uint32_t PATH_MAX_NODES = 32768;

    private:
        struct Agent
        {
            VolumetricAgentState state;
            std::vector<glm::vec3> waypoints;
            size_t nextWaypoint = 0;
            glm::vec3 velocity{0.0f};
            std::shared_ptr<const volumetric::VoxelGrid> grid; // the grid the path was planned on
            float stuckTimer = 0.0f;
            float progressMark = 0.0f; // remaining path length when progress was last seen
        };

        struct Event
        {
            EntityHandle entity;
            bool reached = false; // true: reached; false: failed with `reason`
            std::string reason;
        };

        // (Re)plans `agent` from `from` to its destination; false (Failed + an event) when no path exists, or when a
        // partial path would end within stopDistance of `from` (no further progress possible: "NoPath").
        bool plan(EntityHandle entity, Agent& agent, const glm::vec3& from, float stopDistance,
                  std::vector<Event>& events);
        static void fail(EntityHandle entity, Agent& agent, std::string reason, std::vector<Event>& events);
        static float remainingLength(const Agent& agent, const glm::vec3& position);
        static void publish(const std::vector<Event>& events);

        IVolumetricNavProvider* provider;
        mutable std::mutex mutex;
        std::unordered_map<uint64_t, Agent> agents;
    };
}
