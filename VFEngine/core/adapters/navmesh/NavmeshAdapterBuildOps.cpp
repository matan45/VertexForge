#include "NavmeshAdapter.hpp"

#include <Recast.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>

#include <cstring>
#include <cmath>
#include <glm/glm.hpp>

#include "print/Log.hpp"
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

    struct DetourBuildInput
    {
        const rcConfig* cfg;
        const types::NavmeshBakeSettings* settings;
        rcPolyMesh* pmesh;
        rcPolyMeshDetail* dmesh;
        int tileX = 0;
        int tileZ = 0;
        int tileLayer = 0;
    };

    struct DetourBuildResult
    {
        unsigned char* navData = nullptr;
        int navDataSize = 0;
        explicit operator bool() const { return navData != nullptr; }
    };

    static DetourBuildResult createDetourData(const DetourBuildInput& input)
    {
        auto& pmesh = *input.pmesh;
        auto& dmesh = *input.dmesh;

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
        params.walkableHeight = input.settings->agentHeight;
        params.walkableRadius = input.settings->agentRadius;
        params.walkableClimb = input.settings->agentMaxClimb;
        rcVcopy(params.bmin, pmesh.bmin);
        rcVcopy(params.bmax, pmesh.bmax);
        params.cs = input.cfg->cs;
        params.ch = input.cfg->ch;
        params.buildBvTree = true;
        params.tileX = input.tileX;
        params.tileY = input.tileZ;
        params.tileLayer = input.tileLayer;

        DetourBuildResult result;
        if (!dtCreateNavMeshData(&params, &result.navData, &result.navDataSize))
        {
            vfLogError("NavmeshAdapter: Failed to create Detour navmesh data");
        }
        return result;
    }

    static navigation::NavmeshTileData buildTileData(int tx, int tz,
                                                       const navigation::NavmeshInputGeometry& geometry,
                                                       const types::NavmeshBakeSettings& settings)
    {
        navigation::NavmeshTileData result;
        result.x = tx;
        result.y = tz;

        if (geometry.isEmpty())
            return result;

        const float* verts = geometry.vertices.data();
        const int nVerts = geometry.getVertexCount();
        const int* tris = geometry.triangles.data();
        const int nTris = geometry.getTriangleCount();

        // Use grid-aligned tile bounds, NOT geometry bounds
        // This ensures adjacent tiles share the exact same edge coordinates
        const float tileWorldSize = settings.tileSize * settings.cellSize;
        float bmin[3] = {tx * tileWorldSize, geometry.boundsMin.y, tz * tileWorldSize};
        float bmax[3] = {(tx + 1) * tileWorldSize, geometry.boundsMax.y, (tz + 1) * tileWorldSize};

        rcConfig cfg = createRecastConfig(settings, bmin, bmax);
        cfg.borderSize = cfg.walkableRadius + 3;
        cfg.width = settings.tileSize + cfg.borderSize * 2;
        cfg.height = settings.tileSize + cfg.borderSize * 2;

        float borderExpand = cfg.borderSize * cfg.cs;
        cfg.bmin[0] -= borderExpand;
        cfg.bmin[2] -= borderExpand;
        cfg.bmax[0] += borderExpand;
        cfg.bmax[2] += borderExpand;

        rcContext ctx;

        rcHeightfield* solid = rasterizeGeometry(ctx, cfg, verts, nVerts, tris, nTris);
        if (!solid)
            return result;

        rcCompactHeightfield* chf = buildCompactField(ctx, cfg, *solid);
        rcFreeHeightField(solid);
        if (!chf)
            return result;

        rcPolyMesh* pmesh = nullptr;
        rcPolyMeshDetail* dmesh = nullptr;
        if (!buildPolyMeshes(ctx, cfg, *chf, pmesh, dmesh))
        {
            rcFreeCompactHeightfield(chf);
            return result;
        }
        rcFreeCompactHeightfield(chf);

        if (pmesh->nverts == 0 || pmesh->npolys == 0)
        {
            rcFreePolyMesh(pmesh);
            rcFreePolyMeshDetail(dmesh);
            return result;
        }

        auto detourResult = createDetourData({&cfg, &settings, pmesh, dmesh, tx, tz});
        rcFreePolyMesh(pmesh);
        rcFreePolyMeshDetail(dmesh);

        if (!detourResult)
            return result;

        result.dataSize = static_cast<uint32_t>(detourResult.navDataSize);
        result.data.resize(detourResult.navDataSize);
        memcpy(result.data.data(), detourResult.navData, detourResult.navDataSize);
        dtFree(detourResult.navData);

        return result;
    }

    navigation::NavmeshTileData NavmeshAdapter::buildSingleTile(int tx, int tz,
                                                                  const navigation::NavmeshInputGeometry& geometry,
                                                                  const types::NavmeshBakeSettings& settings)
    {
        return buildTileData(tx, tz, geometry, settings);
    }

    bool NavmeshAdapter::buildNavmesh(const navigation::NavmeshInputGeometry& geometry,
                                       const types::NavmeshBakeSettings& settings)
    {
        updateProgress(types::NavmeshBakeStatus::Collecting, 0.0f, "Preparing geometry");

        if (geometry.isEmpty())
        {
            updateProgress(types::NavmeshBakeStatus::Failed, 0.0f, "No geometry");
            return false;
        }

        const float tileWorldSize = settings.tileSize * settings.cellSize;
        const glm::vec3 bmin = geometry.boundsMin;
        const glm::vec3 bmax = geometry.boundsMax;

        const int tileMinX = static_cast<int>(floorf(bmin.x / tileWorldSize));
        const int tileMinZ = static_cast<int>(floorf(bmin.z / tileWorldSize));
        const int tileMaxX = static_cast<int>(floorf(bmax.x / tileWorldSize));
        const int tileMaxZ = static_cast<int>(floorf(bmax.z / tileWorldSize));
        const int totalTiles = (tileMaxX - tileMinX + 1) * (tileMaxZ - tileMinZ + 1);

        if (!initTiledNavmesh(settings, bmin, bmax))
        {
            updateProgress(types::NavmeshBakeStatus::Failed, 0.0f, "Failed to init tiled navmesh");
            return false;
        }

        updateProgress(types::NavmeshBakeStatus::Building, 0.1f, "Building tiles");

        int tilesBuilt = 0;
        for (int tz = tileMinZ; tz <= tileMaxZ; ++tz)
        {
            for (int tx = tileMinX; tx <= tileMaxX; ++tx)
            {
                // Compute tile AABB with border overlap for geometry clipping
                float tileBminX = tx * tileWorldSize;
                float tileBminZ = tz * tileWorldSize;
                float tileBmaxX = (tx + 1) * tileWorldSize;
                float tileBmaxZ = (tz + 1) * tileWorldSize;

                float borderExpand = settings.agentRadius + settings.cellSize * 3.0f;
                glm::vec3 clipMin(tileBminX - borderExpand, bmin.y, tileBminZ - borderExpand);
                glm::vec3 clipMax(tileBmaxX + borderExpand, bmax.y, tileBmaxZ + borderExpand);

                navigation::NavmeshInputGeometry tileGeometry;
                const float* verts = geometry.vertices.data();
                const int* tris = geometry.triangles.data();
                const int nTris = geometry.getTriangleCount();

                for (int i = 0; i < nTris; ++i)
                {
                    int ia = tris[i * 3];
                    int ib = tris[i * 3 + 1];
                    int ic = tris[i * 3 + 2];

                    glm::vec3 va(verts[ia * 3], verts[ia * 3 + 1], verts[ia * 3 + 2]);
                    glm::vec3 vb(verts[ib * 3], verts[ib * 3 + 1], verts[ib * 3 + 2]);
                    glm::vec3 vc(verts[ic * 3], verts[ic * 3 + 1], verts[ic * 3 + 2]);

                    glm::vec3 triMin = glm::min(va, glm::min(vb, vc));
                    glm::vec3 triMax = glm::max(va, glm::max(vb, vc));

                    if (triMax.x < clipMin.x || triMin.x > clipMax.x ||
                        triMax.z < clipMin.z || triMin.z > clipMax.z)
                        continue;

                    int baseIdx = tileGeometry.getVertexCount();
                    tileGeometry.addVertex(va);
                    tileGeometry.addVertex(vb);
                    tileGeometry.addVertex(vc);
                    tileGeometry.addTriangle(baseIdx, baseIdx + 1, baseIdx + 2);
                }

                if (!tileGeometry.isEmpty())
                {
                    auto tileData = buildTileData(tx, tz, tileGeometry, settings);
                    if (!tileData.data.empty())
                    {
                        addNavmeshTile(tileData);
                    }
                }

                tilesBuilt++;
                float progress = 0.1f + 0.85f * (static_cast<float>(tilesBuilt) / totalTiles);
                updateProgress(types::NavmeshBakeStatus::Building, progress, "Building tiles");
            }
        }

        updateProgress(types::NavmeshBakeStatus::Complete, 1.0f, "Complete");
        vfLogInfo("NavmeshAdapter: Tiled navmesh built ({} tiles)", tilesBuilt);
        return true;
    }
}
