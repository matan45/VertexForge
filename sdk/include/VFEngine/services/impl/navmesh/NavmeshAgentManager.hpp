#pragma once

#include "../../providers/navmesh/INavmeshProvider.hpp"
#include "../../data/EntityHandle.hpp"
#include "../../data/DTOs.hpp"
#include "navigation/FormationSlots.hpp"
#include "navigation/NavmeshData.hpp"
#include <glm/glm.hpp>
#include <unordered_map>
#include <optional>
#include <vector>
#include <functional>

namespace components
{
    struct NavmeshAgentComponent;
}

namespace services
{
    class NavmeshAgentManager
    {
    public:
        using TileCoordFunc = std::function<navigation::NavmeshTileCoord(const glm::vec3&)>;
        using TileVersionFunc = std::function<uint64_t()>;

        struct GroupMoveStatus
        {
            int total = 0;
            int arrived = 0;
            bool complete = false;
        };

        NavmeshAgentManager(INavmeshProvider* provider, TileCoordFunc worldToTileCoord,
                            TileVersionFunc tileVersion = {});

        void addAgent(EntityHandle entity);
        void removeAgent(EntityHandle entity);
        void setAgentDestination(EntityHandle entity, const glm::vec3& target);
        uint64_t setGroupDestination(const std::vector<EntityHandle>& entities,
                                     const glm::vec3& target,
                                     const navigation::FormationParams& formation);
        void stopAgent(EntityHandle entity);
        void updateAgentConfig(EntityHandle entity, float maxSpeed, float maxAcceleration, int rootMotionDriven = -1, float rootMotionSpeedScale = -1.0f, float turnSpeed = -1.0f);
        glm::vec3 getAgentVelocity(EntityHandle entity) const;
        float getAgentSpeed(EntityHandle entity) const;
        GroupMoveStatus getGroupStatus(uint64_t groupId) const;
        std::vector<glm::vec3> getGroupCorridor(uint64_t groupId) const;
        void updatePositions(float deltaTime);

        // VK-1660: the NavmeshAgentComponent's editable fields; nullopt without the component.
        std::optional<NavmeshAgentData> getAgentData(EntityHandle entity) const;
        // Writes every field but inCrowd (false for a missing component or an unusable value, see
        // SetNavmeshAgentDataCommand); a crowd agent takes the new shape, speed and avoidance at once.
        bool setAgentData(EntityHandle entity, const NavmeshAgentData& data);

        // VK-1660: a scene bake re-creates the detour crowd on a worker thread, so between begin and end
        // no agent joins the crowd. begin releases every agent (releaseAllAgents) but keeps the moves in
        // progress; endCrowdRebuild issues them, and the destinations set meanwhile, on the new crowd.
        void beginCrowdRebuild();
        void endCrowdRebuild();

        void suspendAgentsOnUnloadedTiles(const std::vector<navigation::NavmeshTileCoord>& unloadedTiles);
        void resumeAgentsOnLoadedTiles(const std::vector<navigation::NavmeshTileCoord>& loadedTiles);

        // Removes every agent from the crowd, resets each NavmeshAgentComponent's runtime crowd state and
        // forgets all moves, groups and a crowd rebuild in progress.
        void releaseAllAgents();

    private:
        INavmeshProvider* navmeshProvider;
        TileCoordFunc worldToTileCoord;
        TileVersionFunc tileVersion;

        std::unordered_map<uint64_t, int> entityToAgentIndex;
        std::unordered_map<uint64_t, glm::vec3> entityToTarget;
        std::unordered_map<uint64_t, float> entityStuckTimer;
        // Arrived (within arrivalDistance) but not yet within stoppingDistance: the crowd agent is stopped
        // once it gets there.
        std::unordered_map<uint64_t, glm::vec3> pendingStops;
        bool rebuildingCrowd = false;
        std::unordered_map<uint64_t, glm::vec3> deferredDestinations;
        static constexpr float ARRIVAL_DISTANCE = 1.0f;
        static constexpr float STUCK_VELOCITY_THRESHOLD = 0.1f;
        static constexpr float STUCK_TIME_THRESHOLD = 0.5f;

        // Off-mesh destinations (e.g. an RTS click beyond the baked mesh edge) are
        // snapped onto the navmesh, trying progressively wider radii. The 5u default
        // (GetClosestPointQuery) is too small for clicks well past the edge.
        static constexpr float SNAP_SEARCH_RADII[] = {5.0f, 25.0f, 100.0f};
        static constexpr float SNAP_ON_MESH_TOLERANCE = 1.0f;

        struct SuspendedAgent
        {
            uint64_t entityId;
            glm::vec3 position;
            glm::vec3 target;
            bool hasTarget;
            glm::vec3 velocity;
        };
        std::vector<SuspendedAgent> suspendedAgents;

        std::unordered_map<uint64_t, int> velocityTransitionFrames;
        static constexpr int VELOCITY_TRANSITION_FRAMES = 3;

        struct GroupMember
        {
            uint64_t entityId = 0;
            int slot = 0;
            float lateralOffset = 0.0f;
            glm::vec3 slotTarget{0.0f};
            bool arrived = false;
        };

        struct GroupCorridor
        {
            navigation::NavPath corridor;
            uint64_t builtTileVersion = 0;
            glm::vec3 destination{0.0f};
            navigation::FormationParams formation;
            std::vector<GroupMember> members;
        };

        std::unordered_map<uint64_t, GroupCorridor> groups;
        std::unordered_map<uint64_t, uint64_t> entityToGroup;
        uint64_t nextGroupId = 1;
        static constexpr int MAX_GROUP_REPATHS_PER_FRAME = 4;

        bool resolveDestination(const glm::vec3& target, glm::vec3& snapped) const;
        int ensureAgentRegistered(EntityHandle entity);
        // Adds a crowd agent at `position` (a navmesh surface point) shaped by `agent`; -1 on failure.
        int addCrowdAgentFor(const glm::vec3& position, const components::NavmeshAgentComponent& agent);
        void removeEntityFromGroup(uint64_t entityId);
        bool rebuildGroupCorridor(GroupCorridor& group, uint64_t currentVersion);
        void updateGroupSteering(float deltaTime);
        void finishCompletedGroups();
        void holdGroupMemberAtStop(uint64_t entityId);
        void markGroupMemberArrived(uint64_t entityId);
        void markGroupNeedsRepath(uint64_t entityId);
    };
}
