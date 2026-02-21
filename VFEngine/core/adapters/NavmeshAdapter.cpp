#include "NavmeshAdapter.hpp"
#include "print/EditorLogger.hpp"

#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourCrowd.h>
#include <DetourCommon.h>

#include <cstring>
#include <algorithm>

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

    // Default search half-extents for poly queries (x, y, z)
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

    // === Navmesh Building ===

    bool NavmeshAdapter::buildNavmesh(const navigation::NavmeshInputGeometry& geometry,
                                       const types::NavmeshBakeSettings& settings)
    {
        currentProgress.status = types::NavmeshBakeStatus::Collecting;
        currentProgress.progress = 0.0f;
        currentProgress.currentStage = "Preparing geometry";

        const float* verts = geometry.vertices.data();
        const int nVerts = geometry.getVertexCount();
        const int* tris = geometry.triangles.data();
        const int nTris = geometry.getTriangleCount();

        float bmin[3] = {geometry.boundsMin.x, geometry.boundsMin.y, geometry.boundsMin.z};
        float bmax[3] = {geometry.boundsMax.x, geometry.boundsMax.y, geometry.boundsMax.z};

        // Step 1: Initialize build config
        rcConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.cs = settings.cellSize;
        cfg.ch = settings.cellHeight;
        cfg.walkableSlopeAngle = settings.agentMaxSlope;
        cfg.walkableHeight = static_cast<int>(ceilf(settings.agentHeight / cfg.ch));
        cfg.walkableClimb = static_cast<int>(floorf(settings.agentMaxClimb / cfg.ch));
        cfg.walkableRadius = static_cast<int>(ceilf(settings.agentRadius / cfg.cs));
        cfg.maxEdgeLen = static_cast<int>(settings.edgeMaxLen / cfg.cs);
        cfg.maxSimplificationError = settings.edgeMaxError;
        cfg.minRegionArea = settings.regionMinSize * settings.regionMinSize;
        cfg.mergeRegionArea = settings.regionMergeSize * settings.regionMergeSize;
        cfg.maxVertsPerPoly = settings.vertsPerPoly;
        cfg.detailSampleDist = settings.detailSampleDist < 0.9f ? 0 : cfg.cs * settings.detailSampleDist;
        cfg.detailSampleMaxError = cfg.ch * settings.detailSampleMaxError;

        rcVcopy(cfg.bmin, bmin);
        rcVcopy(cfg.bmax, bmax);
        rcCalcGridSize(cfg.bmin, cfg.bmax, cfg.cs, &cfg.width, &cfg.height);

        rcContext ctx;

        // Step 2: Rasterize input polygon soup
        currentProgress.status = types::NavmeshBakeStatus::Voxelizing;
        currentProgress.progress = 0.1f;
        currentProgress.currentStage = "Voxelizing geometry";

        rcHeightfield* solid = rcAllocHeightfield();
        if (!solid || !rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height,
                                            cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
        {
            vfLogError("NavmeshAdapter: Failed to create heightfield");
            rcFreeHeightField(solid);
            currentProgress.status = types::NavmeshBakeStatus::Failed;
            return false;
        }

        std::vector<unsigned char> triAreas(nTris, 0);
        rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts, nVerts, tris, nTris, triAreas.data());
        if (!rcRasterizeTriangles(&ctx, verts, nVerts, tris, triAreas.data(), nTris, *solid, cfg.walkableClimb))
        {
            vfLogError("NavmeshAdapter: Failed to rasterize triangles");
            rcFreeHeightField(solid);
            currentProgress.status = types::NavmeshBakeStatus::Failed;
            return false;
        }

        // Step 3: Filter walkable surfaces
        rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
        rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
        rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);

        currentProgress.progress = 0.3f;
        currentProgress.currentStage = "Building compact heightfield";

        // Step 4: Partition walkable surface to simple regions
        rcCompactHeightfield* chf = rcAllocCompactHeightfield();
        if (!chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid, *chf))
        {
            vfLogError("NavmeshAdapter: Failed to build compact heightfield");
            rcFreeHeightField(solid);
            rcFreeCompactHeightfield(chf);
            currentProgress.status = types::NavmeshBakeStatus::Failed;
            return false;
        }
        rcFreeHeightField(solid);

        if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf))
        {
            vfLogError("NavmeshAdapter: Failed to erode walkable area");
            rcFreeCompactHeightfield(chf);
            currentProgress.status = types::NavmeshBakeStatus::Failed;
            return false;
        }

        currentProgress.status = types::NavmeshBakeStatus::Building;
        currentProgress.progress = 0.5f;
        currentProgress.currentStage = "Building regions";

        if (!rcBuildDistanceField(&ctx, *chf))
        {
            vfLogError("NavmeshAdapter: Failed to build distance field");
            rcFreeCompactHeightfield(chf);
            currentProgress.status = types::NavmeshBakeStatus::Failed;
            return false;
        }

        if (!rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea))
        {
            vfLogError("NavmeshAdapter: Failed to build regions");
            rcFreeCompactHeightfield(chf);
            currentProgress.status = types::NavmeshBakeStatus::Failed;
            return false;
        }

        currentProgress.progress = 0.6f;
        currentProgress.currentStage = "Building contours";

        // Step 5: Trace and simplify region contours
        rcContourSet* cset = rcAllocContourSet();
        if (!cset || !rcBuildContours(&ctx, *chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset))
        {
            vfLogError("NavmeshAdapter: Failed to build contours");
            rcFreeCompactHeightfield(chf);
            rcFreeContourSet(cset);
            currentProgress.status = types::NavmeshBakeStatus::Failed;
            return false;
        }

        currentProgress.progress = 0.7f;
        currentProgress.currentStage = "Building polygon mesh";

        // Step 6: Build polygons mesh from contours
        rcPolyMesh* pmesh = rcAllocPolyMesh();
        if (!pmesh || !rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *pmesh))
        {
            vfLogError("NavmeshAdapter: Failed to build polygon mesh");
            rcFreeCompactHeightfield(chf);
            rcFreeContourSet(cset);
            rcFreePolyMesh(pmesh);
            currentProgress.status = types::NavmeshBakeStatus::Failed;
            return false;
        }

        currentProgress.progress = 0.8f;
        currentProgress.currentStage = "Building detail mesh";

        // Step 7: Create detail mesh for accurate height data
        rcPolyMeshDetail* dmesh = rcAllocPolyMeshDetail();
        if (!dmesh || !rcBuildPolyMeshDetail(&ctx, *pmesh, *chf,
                                              cfg.detailSampleDist, cfg.detailSampleMaxError, *dmesh))
        {
            vfLogError("NavmeshAdapter: Failed to build detail mesh");
            rcFreeCompactHeightfield(chf);
            rcFreeContourSet(cset);
            rcFreePolyMesh(pmesh);
            rcFreePolyMeshDetail(dmesh);
            currentProgress.status = types::NavmeshBakeStatus::Failed;
            return false;
        }

        rcFreeCompactHeightfield(chf);
        rcFreeContourSet(cset);

        currentProgress.progress = 0.9f;
        currentProgress.currentStage = "Creating Detour navmesh";

        // Step 8: Create Detour data from Recast poly mesh
        for (int i = 0; i < pmesh->npolys; ++i)
        {
            pmesh->flags[i] = 1; // Set all polys as walkable
        }

        dtNavMeshCreateParams params;
        memset(&params, 0, sizeof(params));
        params.verts = pmesh->verts;
        params.vertCount = pmesh->nverts;
        params.polys = pmesh->polys;
        params.polyAreas = pmesh->areas;
        params.polyFlags = pmesh->flags;
        params.polyCount = pmesh->npolys;
        params.nvp = pmesh->nvp;
        params.detailMeshes = dmesh->meshes;
        params.detailVerts = dmesh->verts;
        params.detailVertsCount = dmesh->nverts;
        params.detailTris = dmesh->tris;
        params.detailTriCount = dmesh->ntris;
        params.walkableHeight = settings.agentHeight;
        params.walkableRadius = settings.agentRadius;
        params.walkableClimb = settings.agentMaxClimb;
        rcVcopy(params.bmin, pmesh->bmin);
        rcVcopy(params.bmax, pmesh->bmax);
        params.cs = cfg.cs;
        params.ch = cfg.ch;
        params.buildBvTree = true;

        unsigned char* navData = nullptr;
        int navDataSize = 0;
        if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
        {
            vfLogError("NavmeshAdapter: Failed to create Detour navmesh data");
            rcFreePolyMesh(pmesh);
            rcFreePolyMeshDetail(dmesh);
            currentProgress.status = types::NavmeshBakeStatus::Failed;
            return false;
        }

        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);

        // Destroy old navmesh and rebuild under single lock
        std::lock_guard lock(navMeshMutex);
        destroyNavMeshLocked();

        navMesh = dtAllocNavMesh();
        if (!navMesh)
        {
            dtFree(navData);
            currentProgress.status = types::NavmeshBakeStatus::Failed;
            return false;
        }

        dtStatus status = navMesh->init(navData, navDataSize, DT_TILE_FREE_DATA);
        if (dtStatusFailed(status))
        {
            dtFree(navData);
            dtFreeNavMesh(navMesh);
            navMesh = nullptr;
            currentProgress.status = types::NavmeshBakeStatus::Failed;
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
            currentProgress.status = types::NavmeshBakeStatus::Failed;
            return false;
        }

        // Init crowd for agent management
        initCrowd(settings.agentRadius);

        currentProgress.status = types::NavmeshBakeStatus::Complete;
        currentProgress.progress = 1.0f;
        currentProgress.currentStage = "Complete";

        vfLogInfo("NavmeshAdapter: Navmesh built successfully ({} polys)", params.polyCount);
        return true;
    }

    types::NavmeshBakeProgress NavmeshAdapter::getBuildProgress() const
    {
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
            if (tile.data.empty())
                continue;

            unsigned char* data = static_cast<unsigned char*>(dtAlloc(tile.dataSize, DT_ALLOC_PERM));
            if (!data)
                continue;

            memcpy(data, tile.data.data(), tile.dataSize);
            navMesh->addTile(data, tile.dataSize, DT_TILE_FREE_DATA, 0, nullptr);
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

        // Init crowd
        initCrowd(header.settings.agentRadius);

        currentProgress.status = types::NavmeshBakeStatus::Complete;
        currentProgress.progress = 1.0f;
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
        currentProgress = {};
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
        float nearestStart[3], nearestEnd[3];

        navQuery->findNearestPoly(startPos, halfExtents, &filter, &startRef, nearestStart);
        navQuery->findNearestPoly(endPos, halfExtents, &filter, &endRef, nearestEnd);

        if (!startRef || !endRef)
        {
            return result;
        }

        dtPolyRef polys[MAX_POLYS];
        int nPolys = 0;
        navQuery->findPath(startRef, endRef, nearestStart, nearestEnd, &filter, polys, &nPolys, MAX_POLYS);

        if (nPolys <= 0)
        {
            return result;
        }

        // Find straight path through corridor
        float straightPath[MAX_POLYS * 3];
        unsigned char straightPathFlags[MAX_POLYS];
        dtPolyRef straightPathPolys[MAX_POLYS];
        int nStraightPath = 0;

        navQuery->findStraightPath(nearestStart, nearestEnd, polys, nPolys,
                                    straightPath, straightPathFlags, straightPathPolys,
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
                uint32_t baseIdx = static_cast<uint32_t>(outVertices.size());

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
