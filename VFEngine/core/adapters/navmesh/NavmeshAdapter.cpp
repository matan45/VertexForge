#include "NavmeshAdapter.hpp"

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourCrowd.h>
#include <DetourCommon.h>

#include <cstring>
#include <cmath>
#include <algorithm>

#include "types/NavmeshTypes.hpp"
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
        tileGraph.clear();
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

            // Configure default filter (slot 0) with global area costs
            dtQueryFilter* defaultFilter = crowd->getEditableFilter(0);
            if (defaultFilter)
            {
                defaultFilter->setIncludeFlags(0xFFFF);
                defaultFilter->setExcludeFlags(0);
                for (int i = 0; i < 64; ++i)
                    defaultFilter->setAreaCost(i, storedSettings.areaCosts[i]);
            }
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
            storedSettings = header.settings;

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

            // Build tile graph for hierarchical pathfinding
            float tileWorldSize = header.settings.tileSize * header.settings.cellSize;
            if (tileWorldSize > 0.0f)
                tileGraph.rebuild(navMesh, tileWorldSize);
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
        storedSettings = settings;

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

        // Update tile graph for hierarchical pathfinding
        float tileWorldSize = storedSettings.tileSize * storedSettings.cellSize;
        if (tileWorldSize > 0.0f)
            tileGraph.addOrUpdateTile(navMesh, tileData.x, tileData.y, tileWorldSize);

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
        if (dtStatusSucceed(status))
            tileGraph.removeTile(tx, tz);
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

        // Check if hierarchical pathfinding should be used
        float dx = end.x - start.x;
        float dz = end.z - start.z;
        float distXZ = std::sqrt(dx * dx + dz * dz);

        dtQueryFilter filter;
        configureQueryFilter(&filter);

        if (distXZ > storedSettings.hierarchicalPathThreshold && tileGraph.nodeCount() > 1)
        {
            return findPathHierarchical(start, end, agentRadius, agentHeight, filter);
        }

        float startPos[3] = {start.x, start.y, start.z};
        float endPos[3] = {end.x, end.y, end.z};
        float halfExtents[3] = {agentRadius * CROWD_MAX_AGENT_RADIUS_MULT, agentHeight, agentRadius * CROWD_MAX_AGENT_RADIUS_MULT};

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

    void NavmeshAdapter::configureQueryFilter(dtQueryFilter* filter) const
    {
        filter->setIncludeFlags(0xFFFF);
        filter->setExcludeFlags(0);
        for (int i = 0; i < 64; ++i)
            filter->setAreaCost(i, storedSettings.areaCosts[i]);
    }

    void NavmeshAdapter::configureCrowdFilter(int filterIndex, const float* areaCosts, int numAreas)
    {
        std::lock_guard lock(navMeshMutex);
        if (!crowd) return;
        dtQueryFilter* filter = crowd->getEditableFilter(filterIndex);
        if (!filter) return;
        filter->setIncludeFlags(0xFFFF);
        filter->setExcludeFlags(0);
        for (int i = 0; i < numAreas && i < 64; ++i)
            filter->setAreaCost(i, areaCosts[i]);
    }

    void NavmeshAdapter::setCrowdAgentFilterType(int agentIndex, uint8_t filterType)
    {
        std::lock_guard lock(navMeshMutex);
        if (!crowd) return;
        const dtCrowdAgent* ag = crowd->getAgent(agentIndex);
        if (!ag || !ag->active) return;
        dtCrowdAgentParams params = ag->params;
        params.queryFilterType = filterType;
        crowd->updateAgentParameters(agentIndex, &params);
    }

    // === Hierarchical Pathfinding ===

    navigation::NavmeshTileCoord NavmeshAdapter::worldToTileCoord(const glm::vec3& pos) const
    {
        float tileWorldSize = storedSettings.tileSize * storedSettings.cellSize;
        if (tileWorldSize <= 0.0f)
            return {0, 0};
        return {
            static_cast<int32_t>(std::floor(pos.x / tileWorldSize)),
            static_cast<int32_t>(std::floor(pos.z / tileWorldSize))
        };
    }

    void NavmeshAdapter::onTileAdded(int tx, int tz)
    {
        std::lock_guard lock(navMeshMutex);
        if (!navMesh) return;
        float tileWorldSize = storedSettings.tileSize * storedSettings.cellSize;
        tileGraph.addOrUpdateTile(navMesh, tx, tz, tileWorldSize);
    }

    void NavmeshAdapter::onTileRemoved(int tx, int tz)
    {
        std::lock_guard lock(navMeshMutex);
        tileGraph.removeTile(tx, tz);
    }

    navigation::NavPath NavmeshAdapter::findPathHierarchical(
        const glm::vec3& start, const glm::vec3& end,
        float agentRadius, float agentHeight,
        const dtQueryFilter& filter)
    {
        navigation::NavPath result;

        auto startTile = worldToTileCoord(start);
        auto endTile = worldToTileCoord(end);

        if (startTile == endTile)
        {
            // Same tile - use direct A* (should not normally reach here due to threshold)
            float startPos[3] = {start.x, start.y, start.z};
            float endPos[3] = {end.x, end.y, end.z};
            float halfExtents[3] = {agentRadius * CROWD_MAX_AGENT_RADIUS_MULT, agentHeight, agentRadius * CROWD_MAX_AGENT_RADIUS_MULT};

            dtPolyRef startRef = 0, endRef = 0;
            float ns[3], ne[3];
            navQuery->findNearestPoly(startPos, halfExtents, &filter, &startRef, ns);
            navQuery->findNearestPoly(endPos, halfExtents, &filter, &endRef, ne);
            if (!startRef || !endRef) return result;

            static std::vector<dtPolyRef> polys(MAX_POLYS);
            int nPolys = 0;
            navQuery->findPath(startRef, endRef, ns, ne, &filter, polys.data(), &nPolys, MAX_POLYS);
            if (nPolys <= 0) return result;

            static std::vector<float> sp(MAX_POLYS * 3);
            static std::vector<unsigned char> spf(MAX_POLYS);
            static std::vector<dtPolyRef> spp(MAX_POLYS);
            int nSP = 0;
            navQuery->findStraightPath(ns, ne, polys.data(), nPolys, sp.data(), spf.data(), spp.data(), &nSP, MAX_POLYS, 0);

            result.isValid = nSP > 0;
            for (int i = 0; i < nSP; ++i)
                result.waypoints.emplace_back(sp[i*3], sp[i*3+1], sp[i*3+2]);
            return result;
        }

        // Tile-level A*
        auto tilePath = tileGraph.findTilePath(startTile, endTile);
        if (tilePath.empty())
            return result;

        // Build waypoints by stitching local A* segments
        float halfExtents[3] = {agentRadius * CROWD_MAX_AGENT_RADIUS_MULT, agentHeight, agentRadius * CROWD_MAX_AGENT_RADIUS_MULT};

        glm::vec3 segStart = start;

        for (size_t t = 0; t + 1 < tilePath.size(); ++t)
        {
            // Find the best portal between tilePath[t] and tilePath[t+1]
            glm::vec3 segEnd;
            bool lastSegment = (t + 2 >= tilePath.size());

            if (lastSegment)
            {
                segEnd = end;
            }
            else
            {
                // Find portal from tilePath[t] to tilePath[t+1]
                auto nodeIt = tileGraph.hasTile(tilePath[t]) ? &tilePath[t] : nullptr;
                if (!nodeIt) break;

                // Pick the portal closest to the straight line start→end
                glm::vec3 bestPortal = glm::vec3(0.0f);
                float bestDist = std::numeric_limits<float>::max();
                bool foundPortal = false;

                // Search the node's edges for the next tile
                // We need to access the tile graph nodes directly - use a helper
                auto findBestPortal = [&](const navigation::NavmeshTileCoord& fromCoord,
                                           const navigation::NavmeshTileCoord& toCoord) -> glm::vec3
                {
                    // Simple approach: use tile center of the target as intermediate
                    float tws = storedSettings.tileSize * storedSettings.cellSize;
                    return glm::vec3((toCoord.x + 0.5f) * tws, 0.0f, (toCoord.z + 0.5f) * tws);
                };

                segEnd = findBestPortal(tilePath[t], tilePath[t + 1]);
            }

            // Run local Detour A* for this segment
            float sp[3] = {segStart.x, segStart.y, segStart.z};
            float ep[3] = {segEnd.x, segEnd.y, segEnd.z};

            dtPolyRef sRef = 0, eRef = 0;
            float ns[3], ne[3];
            navQuery->findNearestPoly(sp, halfExtents, &filter, &sRef, ns);
            navQuery->findNearestPoly(ep, halfExtents, &filter, &eRef, ne);

            if (!sRef || !eRef)
            {
                segStart = segEnd;
                continue;
            }

            static std::vector<dtPolyRef> polys(MAX_POLYS);
            int nPolys = 0;
            navQuery->findPath(sRef, eRef, ns, ne, &filter, polys.data(), &nPolys, MAX_POLYS);

            if (nPolys > 0)
            {
                static std::vector<float> straightPath(MAX_POLYS * 3);
                static std::vector<unsigned char> straightFlags(MAX_POLYS);
                static std::vector<dtPolyRef> straightPolys(MAX_POLYS);
                int nStraight = 0;
                navQuery->findStraightPath(ns, ne, polys.data(), nPolys,
                    straightPath.data(), straightFlags.data(), straightPolys.data(),
                    &nStraight, MAX_POLYS, 0);

                // Skip first waypoint if it duplicates the last one from previous segment
                int startIdx = (!result.waypoints.empty() && nStraight > 0) ? 1 : 0;
                for (int i = startIdx; i < nStraight; ++i)
                {
                    result.waypoints.emplace_back(
                        straightPath[i * 3],
                        straightPath[i * 3 + 1],
                        straightPath[i * 3 + 2]);
                }
            }

            segStart = segEnd;
        }

        result.isValid = !result.waypoints.empty();
        return result;
    }

}
