#include "NavmeshAdapter.hpp"

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourCrowd.h>
#include <DetourCommon.h>

#include <cstring>
#include <cmath>
#include <algorithm>

#include "print/Log.hpp"
namespace core
{
    static constexpr int MAX_POLYS = 2048;
    static constexpr int MAX_CROWD_AGENTS = 128;

    // Recast/Detour recommended multipliers (see dtCrowdAgentParams docs)
    static constexpr float CROWD_MAX_AGENT_RADIUS_MULT = 4.0f;
    static constexpr float COLLISION_QUERY_RANGE_MULT = 12.0f;
    static constexpr float PATH_OPT_RANGE_MULT = 30.0f;
    static constexpr float AGENT_SEPARATION_WEIGHT = 2.0f;
    // Highest-quality avoidance (Detour provides types 0-3)
    static constexpr int OBSTACLE_AVOIDANCE_TYPE = 3;

    static constexpr float TARGET_HALF_EXTENTS[3] = {2.0f, 4.0f, 2.0f};

    NavmeshAdapter::NavmeshAdapter() = default;

    NavmeshAdapter::~NavmeshAdapter()
    {
        cleanUp();
    }

    bool NavmeshAdapter::init()
    {
        initialized = true;
        return true;
    }

    void NavmeshAdapter::cleanUp()
    {
        destroyNavMesh();
        initialized = false;
    }

    bool NavmeshAdapter::isInitialized() const
    {
        return initialized;
    }

    void NavmeshAdapter::destroyNavMesh()
    {
        std::lock_guard lock(navMeshMutex);
        destroyNavMeshLocked();
    }

    void NavmeshAdapter::destroyNavMeshLocked()
    {
        if (crowd)
        {
            dtFreeCrowd(crowd);
            crowd = nullptr;
        }
        if (navQuery)
        {
            dtFreeNavMeshQuery(navQuery);
            navQuery = nullptr;
        }
        if (navMesh)
        {
            dtFreeNavMesh(navMesh);
            navMesh = nullptr;
        }
    }

    void NavmeshAdapter::initCrowd(float agentRadius)
    {
        if (!navMesh)
            return;

        if (crowd)
        {
            dtFreeCrowd(crowd);
        }
        crowd = dtAllocCrowd();
        if (crowd)
        {
            crowd->init(MAX_CROWD_AGENTS, agentRadius * CROWD_MAX_AGENT_RADIUS_MULT, navMesh);
        }
    }

    bool NavmeshAdapter::initializeNavmesh(unsigned char* navData, int navDataSize, float agentRadius)
    {
        std::lock_guard lock(navMeshMutex);
        destroyNavMeshLocked();

        navMesh = dtAllocNavMesh();
        if (!navMesh)
        {
            dtFree(navData);
            return false;
        }

        dtStatus status = navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
        if (dtStatusFailed(status))
        {
            dtFree(navData);
            dtFreeNavMesh(navMesh);
            navMesh = nullptr;
            return false;
        }

        navQuery = dtAllocNavMeshQuery();
        if (!navQuery || dtStatusFailed(navQuery->init(navMesh, MAX_POLYS)))
        {
            vfLogError("NavmeshAdapter: Failed to create navmesh query");
            dtFreeNavMeshQuery(navQuery);
            navQuery = nullptr;
            dtFreeNavMesh(navMesh);
            navMesh = nullptr;
            return false;
        }

        initCrowd(agentRadius);
        return true;
    }

    void NavmeshAdapter::updateProgress(types::NavmeshBakeStatus status, float progress, const char* stage)
    {
        std::lock_guard lock(progressMutex);
        currentProgress.status = status;
        currentProgress.progress = progress;
        currentProgress.currentStage = stage;
    }

    types::NavmeshBakeProgress NavmeshAdapter::getBuildProgress() const
    {
        std::lock_guard lock(progressMutex);
        return currentProgress;
    }

    // === Serialization ===

    std::vector<navigation::NavmeshTileData> NavmeshAdapter::serializeNavmesh() const
    {
        std::lock_guard lock(navMeshMutex);
        std::vector<navigation::NavmeshTileData> result;

        if (!navMesh)
            return result;

        const dtNavMesh* mesh = navMesh;
        for (int i = 0; i < mesh->getMaxTiles(); ++i)
        {
            const dtMeshTile* tile = mesh->getTile(i);
            if (!tile || !tile->header || !tile->dataSize)
                continue;

            navigation::NavmeshTileData tileData;
            tileData.x = tile->header->x;
            tileData.y = tile->header->y;
            tileData.dataSize = static_cast<uint32_t>(tile->dataSize);
            tileData.data.resize(tile->dataSize);
            memcpy(tileData.data.data(), tile->data, tile->dataSize);
            result.push_back(std::move(tileData));
        }

        return result;
    }

    bool NavmeshAdapter::deserializeNavmesh(const navigation::NavmeshFileHeader& header,
                                              const std::vector<navigation::NavmeshTileData>& tiles)
    {
        {
            std::lock_guard lock(navMeshMutex);
            destroyNavMeshLocked();

            navMesh = dtAllocNavMesh();
            if (!navMesh)
                return false;

            dtNavMeshParams meshParams;
            memset(&meshParams, 0, sizeof(meshParams));
            meshParams.orig[0] = header.boundsMin.x;
            meshParams.orig[1] = header.boundsMin.y;
            meshParams.orig[2] = header.boundsMin.z;
            meshParams.tileWidth = header.settings.tileSize * header.settings.cellSize;
            meshParams.tileHeight = header.settings.tileSize * header.settings.cellSize;
            meshParams.maxTiles = std::max(1u, header.tileCount);
            meshParams.maxPolys = MAX_POLYS;

            dtStatus status = navMesh->init(&meshParams);
            if (dtStatusFailed(status))
            {
                dtFreeNavMesh(navMesh);
                navMesh = nullptr;
                return false;
            }

            for (const auto& tile : tiles)
            {
                if (tile.data.empty() || tile.dataSize > tile.data.size())
                    continue;

                unsigned char* data = static_cast<unsigned char*>(dtAlloc(tile.dataSize, DT_ALLOC_PERM));
                if (!data)
                    continue;

                memcpy(data, tile.data.data(), tile.dataSize);
                dtStatus tileStatus = navMesh->addTile(data, tile.dataSize, DT_TILE_FREE_DATA, 0, nullptr);
                if (dtStatusFailed(tileStatus))
                {
                    dtFree(data);
                }
            }

            navQuery = dtAllocNavMeshQuery();
            if (!navQuery || dtStatusFailed(navQuery->init(navMesh, MAX_POLYS)))
            {
                dtFreeNavMeshQuery(navQuery);
                navQuery = nullptr;
                dtFreeNavMesh(navMesh);
                navMesh = nullptr;
                return false;
            }

            initCrowd(header.settings.agentRadius);
        }

        updateProgress(types::NavmeshBakeStatus::Complete, 1.0f, "Complete");
        return true;
    }

    bool NavmeshAdapter::hasNavmesh() const
    {
        std::lock_guard lock(navMeshMutex);
        return navMesh != nullptr;
    }

    void NavmeshAdapter::clearNavmesh()
    {
        destroyNavMesh();
        updateProgress(types::NavmeshBakeStatus::Idle, 0.0f, "");
    }

    // === Tiled Navmesh ===

    bool NavmeshAdapter::initTiledNavmesh(const types::NavmeshBakeSettings& settings,
                                            const glm::vec3& boundsMin, const glm::vec3& boundsMax)
    {
        std::lock_guard lock(navMeshMutex);
        destroyNavMeshLocked();

        navMesh = dtAllocNavMesh();
        if (!navMesh)
            return false;

        const float tileWorldSize = settings.tileSize * settings.cellSize;
        // Compute tile range from actual grid coordinates (origin is 0,0)
        const int tileMinX = static_cast<int>(floorf(boundsMin.x / tileWorldSize));
        const int tileMinZ = static_cast<int>(floorf(boundsMin.z / tileWorldSize));
        const int tileMaxX = static_cast<int>(floorf(boundsMax.x / tileWorldSize));
        const int tileMaxZ = static_cast<int>(floorf(boundsMax.z / tileWorldSize));
        const int gridW = tileMaxX - tileMinX + 1;
        const int gridH = tileMaxZ - tileMinZ + 1;
        const int maxTiles = std::max(1, gridW * gridH);

        dtNavMeshParams meshParams;
        memset(&meshParams, 0, sizeof(meshParams));
        // Origin must be (0,y,0) because tile coords are on a global grid:
        // tile (tx,tz) maps to world [tx*tileSize, (tx+1)*tileSize]
        meshParams.orig[0] = 0.0f;
        meshParams.orig[1] = boundsMin.y;
        meshParams.orig[2] = 0.0f;
        meshParams.tileWidth = tileWorldSize;
        meshParams.tileHeight = tileWorldSize;
        meshParams.maxTiles = maxTiles;
        meshParams.maxPolys = MAX_POLYS;

        dtStatus status = navMesh->init(&meshParams);
        if (dtStatusFailed(status))
        {
            dtFreeNavMesh(navMesh);
            navMesh = nullptr;
            return false;
        }

        navQuery = dtAllocNavMeshQuery();
        if (!navQuery || dtStatusFailed(navQuery->init(navMesh, MAX_POLYS)))
        {
            dtFreeNavMeshQuery(navQuery);
            navQuery = nullptr;
            dtFreeNavMesh(navMesh);
            navMesh = nullptr;
            return false;
        }

        initCrowd(settings.agentRadius);
        return true;
    }

    bool NavmeshAdapter::addNavmeshTile(const navigation::NavmeshTileData& tileData)
    {
        std::lock_guard lock(navMeshMutex);
        if (!navMesh || tileData.data.empty())
            return false;

        unsigned char* data = static_cast<unsigned char*>(dtAlloc(tileData.dataSize, DT_ALLOC_PERM));
        if (!data)
            return false;

        memcpy(data, tileData.data.data(), tileData.dataSize);

        dtTileRef existingRef = navMesh->getTileRefAt(tileData.x, tileData.y, 0);
        if (existingRef)
        {
            navMesh->removeTile(existingRef, nullptr, nullptr);
        }

        dtStatus status = navMesh->addTile(data, tileData.dataSize, DT_TILE_FREE_DATA, 0, nullptr);
        if (dtStatusFailed(status))
        {
            dtFree(data);
            return false;
        }
        return true;
    }

    bool NavmeshAdapter::removeNavmeshTile(int tx, int tz)
    {
        std::lock_guard lock(navMeshMutex);
        if (!navMesh)
            return false;

        dtTileRef ref = navMesh->getTileRefAt(tx, tz, 0);
        if (!ref)
            return false;

        dtStatus status = navMesh->removeTile(ref, nullptr, nullptr);
        return dtStatusSucceed(status);
    }

    // === Pathfinding ===

    navigation::NavPath NavmeshAdapter::findPath(const glm::vec3& start, const glm::vec3& end,
                                                   float agentRadius, float agentHeight)
    {
        std::lock_guard lock(navMeshMutex);
        navigation::NavPath result;

        if (!navMesh || !navQuery)
            return result;

        float startPos[3] = {start.x, start.y, start.z};
        float endPos[3] = {end.x, end.y, end.z};
        float halfExtents[3] = {agentRadius * CROWD_MAX_AGENT_RADIUS_MULT, agentHeight, agentRadius * CROWD_MAX_AGENT_RADIUS_MULT};

        dtQueryFilter filter;
        filter.setIncludeFlags(0xFFFF);
        filter.setExcludeFlags(0);

        dtPolyRef startRef = 0, endRef = 0;
        float nearestStart[3] = {0.0f, 0.0f, 0.0f};
        float nearestEnd[3] = {0.0f, 0.0f, 0.0f};

        dtStatus startStatus = navQuery->findNearestPoly(startPos, halfExtents, &filter, &startRef, nearestStart);
        dtStatus endStatus = navQuery->findNearestPoly(endPos, halfExtents, &filter, &endRef, nearestEnd);

        if (dtStatusFailed(startStatus) || dtStatusFailed(endStatus) || !startRef || !endRef)
        {
            return result;
        }

        static std::vector<dtPolyRef> polys(MAX_POLYS);
        int nPolys = 0;
        navQuery->findPath(startRef, endRef, nearestStart, nearestEnd, &filter, polys.data(), &nPolys, MAX_POLYS);

        if (nPolys <= 0)
        {
            return result;
        }

        static std::vector<float> straightPath(MAX_POLYS * 3);
        static std::vector<unsigned char> straightPathFlags(MAX_POLYS);
        static std::vector<dtPolyRef> straightPathPolys(MAX_POLYS);
        int nStraightPath = 0;

        navQuery->findStraightPath(nearestStart, nearestEnd, polys.data(), nPolys,
                                    straightPath.data(), straightPathFlags.data(), straightPathPolys.data(),
                                    &nStraightPath, MAX_POLYS, 0);

        result.isValid = nStraightPath > 0;
        result.isPartial = (polys[nPolys - 1] != endRef);

        for (int i = 0; i < nStraightPath; ++i)
        {
            result.waypoints.emplace_back(
                straightPath[i * 3],
                straightPath[i * 3 + 1],
                straightPath[i * 3 + 2]);
        }

        return result;
    }

    navigation::NavmeshRaycastResult NavmeshAdapter::navmeshRaycast(const glm::vec3& from, const glm::vec3& to)
    {
        std::lock_guard lock(navMeshMutex);
        navigation::NavmeshRaycastResult result;

        if (!navMesh || !navQuery)
            return result;

        float fromPos[3] = {from.x, from.y, from.z};
        float toPos[3] = {to.x, to.y, to.z};

        dtQueryFilter filter;
        filter.setIncludeFlags(0xFFFF);
        filter.setExcludeFlags(0);

        dtPolyRef startRef = 0;
        float nearestStart[3] = {0.0f, 0.0f, 0.0f};
        dtStatus status = navQuery->findNearestPoly(fromPos, TARGET_HALF_EXTENTS, &filter, &startRef, nearestStart);

        if (dtStatusFailed(status) || !startRef)
            return result;

        float t = 0.0f;
        float hitNormal[3] = {0.0f, 0.0f, 0.0f};
        dtPolyRef polys[MAX_POLYS];
        int nPolys = 0;

        navQuery->raycast(startRef, nearestStart, toPos, &filter, &t, hitNormal, polys, &nPolys, MAX_POLYS);

        if (t < 1.0f)
        {
            result.hit = true;
            result.hitPoint = from + t * (to - from);
            result.hitDistance = glm::distance(from, result.hitPoint);
        }

        return result;
    }

    glm::vec3 NavmeshAdapter::getClosestPoint(const glm::vec3& point, float searchRadius)
    {
        std::lock_guard lock(navMeshMutex);

        if (!navMesh || !navQuery)
            return point;

        float pos[3] = {point.x, point.y, point.z};
        float halfExtents[3] = {searchRadius, searchRadius, searchRadius};

        dtQueryFilter filter;
        filter.setIncludeFlags(0xFFFF);

        dtPolyRef ref = 0;
        float nearest[3];
        navQuery->findNearestPoly(pos, halfExtents, &filter, &ref, nearest);

        if (ref)
        {
            return {nearest[0], nearest[1], nearest[2]};
        }
        return point;
    }

    bool NavmeshAdapter::isPointOnNavmesh(const glm::vec3& point, float tolerance)
    {
        std::lock_guard lock(navMeshMutex);

        if (!navMesh || !navQuery)
            return false;

        float pos[3] = {point.x, point.y, point.z};
        float halfExtents[3] = {tolerance, tolerance, tolerance};

        dtQueryFilter filter;
        filter.setIncludeFlags(0xFFFF);

        dtPolyRef ref = 0;
        float nearest[3];
        navQuery->findNearestPoly(pos, halfExtents, &filter, &ref, nearest);

        return ref != 0;
    }

    // === Crowd / Agent ===

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
        {
            crowd->removeAgent(agentIndex);
        }
    }

    void NavmeshAdapter::setCrowdAgentTarget(int agentIndex, const glm::vec3& target)
    {
        std::lock_guard lock(navMeshMutex);
        if (!crowd || !navQuery)
            return;

        float pos[3] = {target.x, target.y, target.z};
        float halfExtents[3] = {TARGET_HALF_EXTENTS[0], TARGET_HALF_EXTENTS[1], TARGET_HALF_EXTENTS[2]};

        dtQueryFilter filter;
        filter.setIncludeFlags(0xFFFF);

        dtPolyRef ref = 0;
        float nearest[3];
        navQuery->findNearestPoly(pos, halfExtents, &filter, &ref, nearest);

        if (ref)
        {
            crowd->requestMoveTarget(agentIndex, ref, nearest);
        }
    }

    void NavmeshAdapter::stopCrowdAgent(int agentIndex)
    {
        std::lock_guard lock(navMeshMutex);
        if (crowd)
        {
            crowd->resetMoveTarget(agentIndex);
        }
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
        {
            crowd->update(deltaTime, nullptr);
        }
    }

    // === Debug ===

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
                        {
                            v = &tile->verts[poly->verts[t[l]] * 3];
                        }
                        else
                        {
                            v = &tile->detailVerts[(pd->vertBase + t[l] - poly->vertCount) * 3];
                        }
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
