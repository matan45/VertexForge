#include "NavmeshAdapter.hpp"

#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>

#include <cstring>

namespace core
{
    static rcConfig createRecastConfig(const types::NavmeshBakeSettings& settings,
                                        const float* bmin, const float* bmax)
    {
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
        return cfg;
    }

    static rcHeightfield* rasterizeGeometry(rcContext& ctx, const rcConfig& cfg,
                                             const float* verts, int nVerts,
                                             const int* tris, int nTris)
    {
        rcHeightfield* solid = rcAllocHeightfield();
        if (!solid || !rcCreateHeightfield(&ctx, *solid, cfg.width, cfg.height,
                                            cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
        {
            vfLogError("NavmeshAdapter: Failed to create heightfield");
            rcFreeHeightField(solid);
            return nullptr;
        }

        std::vector<unsigned char> triAreas(nTris, 0);
        rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, verts, nVerts, tris, nTris, triAreas.data());
        if (!rcRasterizeTriangles(&ctx, verts, nVerts, tris, triAreas.data(), nTris, *solid, cfg.walkableClimb))
        {
            vfLogError("NavmeshAdapter: Failed to rasterize triangles");
            rcFreeHeightField(solid);
            return nullptr;
        }

        rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *solid);
        rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *solid);
        rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *solid);
        return solid;
    }

    static rcCompactHeightfield* buildCompactField(rcContext& ctx, const rcConfig& cfg,
                                                    rcHeightfield& solid)
    {
        rcCompactHeightfield* chf = rcAllocCompactHeightfield();
        if (!chf || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, solid, *chf))
        {
            vfLogError("NavmeshAdapter: Failed to build compact heightfield");
            rcFreeCompactHeightfield(chf);
            return nullptr;
        }

        if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *chf))
        {
            vfLogError("NavmeshAdapter: Failed to erode walkable area");
            rcFreeCompactHeightfield(chf);
            return nullptr;
        }

        if (!rcBuildDistanceField(&ctx, *chf))
        {
            vfLogError("NavmeshAdapter: Failed to build distance field");
            rcFreeCompactHeightfield(chf);
            return nullptr;
        }

        if (!rcBuildRegions(&ctx, *chf, 0, cfg.minRegionArea, cfg.mergeRegionArea))
        {
            vfLogError("NavmeshAdapter: Failed to build regions");
            rcFreeCompactHeightfield(chf);
            return nullptr;
        }

        return chf;
    }

    static bool buildPolyMeshes(rcContext& ctx, const rcConfig& cfg,
                                 rcCompactHeightfield& chf,
                                 rcPolyMesh*& outPmesh, rcPolyMeshDetail*& outDmesh)
    {
        rcContourSet* cset = rcAllocContourSet();
        if (!cset || !rcBuildContours(&ctx, chf, cfg.maxSimplificationError, cfg.maxEdgeLen, *cset))
        {
            vfLogError("NavmeshAdapter: Failed to build contours");
            rcFreeContourSet(cset);
            return false;
        }

        outPmesh = rcAllocPolyMesh();
        if (!outPmesh || !rcBuildPolyMesh(&ctx, *cset, cfg.maxVertsPerPoly, *outPmesh))
        {
            vfLogError("NavmeshAdapter: Failed to build polygon mesh");
            rcFreeContourSet(cset);
            rcFreePolyMesh(outPmesh);
            outPmesh = nullptr;
            return false;
        }

        outDmesh = rcAllocPolyMeshDetail();
        if (!outDmesh || !rcBuildPolyMeshDetail(&ctx, *outPmesh, chf,
                                                  cfg.detailSampleDist, cfg.detailSampleMaxError, *outDmesh))
        {
            vfLogError("NavmeshAdapter: Failed to build detail mesh");
            rcFreeContourSet(cset);
            rcFreePolyMesh(outPmesh);
            rcFreePolyMeshDetail(outDmesh);
            outPmesh = nullptr;
            outDmesh = nullptr;
            return false;
        }

        rcFreeContourSet(cset);
        return true;
    }

    static bool createDetourData(const rcConfig& cfg, const types::NavmeshBakeSettings& settings,
                                  rcPolyMesh& pmesh, rcPolyMeshDetail& dmesh,
                                  unsigned char*& outNavData, int& outNavDataSize)
    {
        for (int i = 0; i < pmesh.npolys; ++i)
            pmesh.flags[i] = 1;

        dtNavMeshCreateParams params;
        memset(&params, 0, sizeof(params));
        params.verts = pmesh.verts;
        params.vertCount = pmesh.nverts;
        params.polys = pmesh.polys;
        params.polyAreas = pmesh.areas;
        params.polyFlags = pmesh.flags;
        params.polyCount = pmesh.npolys;
        params.nvp = pmesh.nvp;
        params.detailMeshes = dmesh.meshes;
        params.detailVerts = dmesh.verts;
        params.detailVertsCount = dmesh.nverts;
        params.detailTris = dmesh.tris;
        params.detailTriCount = dmesh.ntris;
        params.walkableHeight = settings.agentHeight;
        params.walkableRadius = settings.agentRadius;
        params.walkableClimb = settings.agentMaxClimb;
        rcVcopy(params.bmin, pmesh.bmin);
        rcVcopy(params.bmax, pmesh.bmax);
        params.cs = cfg.cs;
        params.ch = cfg.ch;
        params.buildBvTree = true;

        if (!dtCreateNavMeshData(&params, &outNavData, &outNavDataSize))
        {
            vfLogError("NavmeshAdapter: Failed to create Detour navmesh data");
            return false;
        }
        return true;
    }

    bool NavmeshAdapter::buildNavmesh(const navigation::NavmeshInputGeometry& geometry,
                                       const types::NavmeshBakeSettings& settings)
    {
        updateProgress(types::NavmeshBakeStatus::Collecting, 0.0f, "Preparing geometry");

        const float* verts = geometry.vertices.data();
        const int nVerts = geometry.getVertexCount();
        const int* tris = geometry.triangles.data();
        const int nTris = geometry.getTriangleCount();
        float bmin[3] = {geometry.boundsMin.x, geometry.boundsMin.y, geometry.boundsMin.z};
        float bmax[3] = {geometry.boundsMax.x, geometry.boundsMax.y, geometry.boundsMax.z};

        rcConfig cfg = createRecastConfig(settings, bmin, bmax);
        rcContext ctx;

        updateProgress(types::NavmeshBakeStatus::Voxelizing, 0.1f, "Voxelizing geometry");

        rcHeightfield* solid = rasterizeGeometry(ctx, cfg, verts, nVerts, tris, nTris);
        if (!solid)
        {
            updateProgress(types::NavmeshBakeStatus::Failed, 0.0f, "Failed");
            return false;
        }

        updateProgress(types::NavmeshBakeStatus::Building, 0.3f, "Building regions");

        rcCompactHeightfield* chf = buildCompactField(ctx, cfg, *solid);
        rcFreeHeightField(solid);
        if (!chf)
        {
            updateProgress(types::NavmeshBakeStatus::Failed, 0.0f, "Failed");
            return false;
        }

        updateProgress(types::NavmeshBakeStatus::Building, 0.6f, "Building polygon mesh");

        rcPolyMesh* pmesh = nullptr;
        rcPolyMeshDetail* dmesh = nullptr;
        if (!buildPolyMeshes(ctx, cfg, *chf, pmesh, dmesh))
        {
            rcFreeCompactHeightfield(chf);
            updateProgress(types::NavmeshBakeStatus::Failed, 0.0f, "Failed");
            return false;
        }
        rcFreeCompactHeightfield(chf);

        updateProgress(types::NavmeshBakeStatus::Building, 0.9f, "Creating Detour navmesh");

        unsigned char* navData = nullptr;
        int navDataSize = 0;
        if (!createDetourData(cfg, settings, *pmesh, *dmesh, navData, navDataSize))
        {
            rcFreePolyMesh(pmesh);
            rcFreePolyMeshDetail(dmesh);
            updateProgress(types::NavmeshBakeStatus::Failed, 0.0f, "Failed");
            return false;
        }
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);

        if (!initializeNavmesh(navData, navDataSize, settings.agentRadius))
        {
            updateProgress(types::NavmeshBakeStatus::Failed, 0.0f, "Failed");
            return false;
        }

        updateProgress(types::NavmeshBakeStatus::Complete, 1.0f, "Complete");
        vfLogInfo("NavmeshAdapter: Navmesh built successfully");
        return true;
    }
}
