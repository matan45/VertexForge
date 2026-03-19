#include "NavmeshAdapter.hpp"
#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourCrowd.h>
#include <cstring>

namespace core
{
    static constexpr float CROWD_MAX_AGENT_RADIUS_MULT = 4.0f;
    static constexpr float COLLISION_QUERY_RANGE_MULT = 12.0f;
    static constexpr float PATH_OPT_RANGE_MULT = 30.0f;
    static constexpr float AGENT_SEPARATION_WEIGHT = 2.0f;
    static constexpr int OBSTACLE_AVOIDANCE_TYPE = 3;
    static constexpr float CROWD_TARGET_HALF_EXTENTS[3] = {2.0f, 4.0f, 2.0f};

    int NavmeshAdapter::addCrowdAgent(const glm::vec3& position, float radius, float height,
                                       float maxSpeed, float maxAcceleration)
    {
        std::lock_guard lock(navMeshMutex);
        if (!crowd)
            return -1;

        dtCrowdAgentParams ap;
        memset(&ap, 0, sizeof(ap));
        ap.radius = radius;
        ap.height = height;
        ap.maxAcceleration = maxAcceleration;
        ap.maxSpeed = maxSpeed;
        ap.collisionQueryRange = radius * COLLISION_QUERY_RANGE_MULT;
        ap.pathOptimizationRange = radius * PATH_OPT_RANGE_MULT;
        ap.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS |
                         DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_OBSTACLE_AVOIDANCE;
        ap.obstacleAvoidanceType = OBSTACLE_AVOIDANCE_TYPE;
        ap.separationWeight = AGENT_SEPARATION_WEIGHT;

        float pos[3] = {position.x, position.y, position.z};
        return crowd->addAgent(pos, &ap);
    }

    void NavmeshAdapter::removeCrowdAgent(int agentIndex)
    {
        std::lock_guard lock(navMeshMutex);
        if (crowd)
            crowd->removeAgent(agentIndex);
    }

    void NavmeshAdapter::setCrowdAgentTarget(int agentIndex, const glm::vec3& target)
    {
        std::lock_guard lock(navMeshMutex);
        if (!crowd || !navQuery)
            return;

        float pos[3] = {target.x, target.y, target.z};
        float halfExtents[3] = {CROWD_TARGET_HALF_EXTENTS[0], CROWD_TARGET_HALF_EXTENTS[1], CROWD_TARGET_HALF_EXTENTS[2]};

        dtQueryFilter filter;
        filter.setIncludeFlags(0xFFFF);

        dtPolyRef ref = 0;
        float nearest[3];
        navQuery->findNearestPoly(pos, halfExtents, &filter, &ref, nearest);

        if (ref)
            crowd->requestMoveTarget(agentIndex, ref, nearest);
    }

    void NavmeshAdapter::stopCrowdAgent(int agentIndex)
    {
        std::lock_guard lock(navMeshMutex);
        if (crowd)
            crowd->resetMoveTarget(agentIndex);
    }

    void NavmeshAdapter::updateCrowdAgentParams(int agentIndex, float maxSpeed, float maxAcceleration)
    {
        std::lock_guard lock(navMeshMutex);
        if (!crowd) return;

        const dtCrowdAgent* ag = crowd->getAgent(agentIndex);
        if (!ag || !ag->active) return;

        dtCrowdAgentParams params = ag->params;
        if (maxSpeed >= 0.0f) params.maxSpeed = maxSpeed;
        if (maxAcceleration >= 0.0f) params.maxAcceleration = maxAcceleration;
        crowd->updateAgentParameters(agentIndex, &params);
    }

    glm::vec3 NavmeshAdapter::getCrowdAgentPosition(int agentIndex) const
    {
        std::lock_guard lock(navMeshMutex);
        if (!crowd)
            return {0.0f, 0.0f, 0.0f};

        const dtCrowdAgent* ag = crowd->getAgent(agentIndex);
        if (!ag || !ag->active)
            return {0.0f, 0.0f, 0.0f};

        return {ag->npos[0], ag->npos[1], ag->npos[2]};
    }

    glm::vec3 NavmeshAdapter::getCrowdAgentVelocity(int agentIndex) const
    {
        std::lock_guard lock(navMeshMutex);
        if (!crowd)
            return {0.0f, 0.0f, 0.0f};

        const dtCrowdAgent* ag = crowd->getAgent(agentIndex);
        if (!ag || !ag->active)
            return {0.0f, 0.0f, 0.0f};

        return {ag->vel[0], ag->vel[1], ag->vel[2]};
    }

    float NavmeshAdapter::getCrowdAgentMaxSpeed(int agentIndex) const
    {
        std::lock_guard lock(navMeshMutex);
        if (!crowd)
            return 0.0f;

        const dtCrowdAgent* ag = crowd->getAgent(agentIndex);
        if (!ag || !ag->active)
            return 0.0f;

        return ag->params.maxSpeed;
    }

    void NavmeshAdapter::updateCrowd(float deltaTime)
    {
        std::lock_guard lock(navMeshMutex);
        if (crowd)
            crowd->update(deltaTime, nullptr);
    }

    void NavmeshAdapter::getDebugMesh(std::vector<glm::vec3>& outVertices,
                                       std::vector<uint32_t>& outIndices) const
    {
        std::lock_guard lock(navMeshMutex);
        outVertices.clear();
        outIndices.clear();

        if (!navMesh)
            return;

        const dtNavMesh* mesh = navMesh;

        int totalTris = 0;
        for (int i = 0; i < mesh->getMaxTiles(); ++i)
        {
            const dtMeshTile* tile = mesh->getTile(i);
            if (!tile || !tile->header)
                continue;
            for (int j = 0; j < tile->header->polyCount; ++j)
            {
                if (tile->polys[j].getType() != DT_POLYTYPE_OFFMESH_CONNECTION)
                    totalTris += tile->detailMeshes[j].triCount;
            }
        }

        outVertices.reserve(totalTris * 3);
        outIndices.reserve(totalTris * 3);

        for (int i = 0; i < mesh->getMaxTiles(); ++i)
        {
            const dtMeshTile* tile = mesh->getTile(i);
            if (!tile || !tile->header)
                continue;

            for (int j = 0; j < tile->header->polyCount; ++j)
            {
                const dtPoly* poly = &tile->polys[j];
                if (poly->getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
                    continue;

                const dtPolyDetail* pd = &tile->detailMeshes[j];
                for (int k = 0; k < pd->triCount; ++k)
                {
                    const unsigned char* t = &tile->detailTris[(pd->triBase + k) * 4];
                    for (int l = 0; l < 3; ++l)
                    {
                        const float* v;
                        if (t[l] < poly->vertCount)
                            v = &tile->verts[poly->verts[t[l]] * 3];
                        else
                            v = &tile->detailVerts[(pd->vertBase + t[l] - poly->vertCount) * 3];
                        outVertices.emplace_back(v[0], v[1], v[2]);
                    }

                    uint32_t idx = static_cast<uint32_t>(outVertices.size()) - 3;
                    outIndices.push_back(idx);
                    outIndices.push_back(idx + 1);
                    outIndices.push_back(idx + 2);
                }
            }
        }
    }
}
