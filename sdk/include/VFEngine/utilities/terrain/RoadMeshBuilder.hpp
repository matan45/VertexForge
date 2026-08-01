#pragma once

#include "RoadMeshTypes.hpp"

#include <glm/glm.hpp>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

// VK-1621. Pure-math road ribbon generator. Deliberately header-only, dependency-free
// (no Services events, no TerrainGrid, no Import) and parameterised on a height callback,
// so it is unit-testable against a synthetic terrain — see VFEngine/tests/test_road_mesh.cpp.
//
// The caller supplies `heightFn`, which MUST reproduce the terrain's ACTUAL rendered surface.
// Terrain quads are split on the anti-diagonal (TerrainTileGenerator.cpp:274-286), so plain
// bilinear is wrong by the full ridge amplitude down the hinge and would sink the road into
// the terrain; use terrain::terrainQuadHeight (TerrainRVTBudget.hpp:104). `heightFn` must
// always return a usable height — out-of-bounds policy belongs to the caller.
namespace terrain
{
    using RoadHeightFn = std::function<float(float worldX, float worldZ)>;

    namespace roaddetail
    {
        struct ResampledCurve
        {
            std::vector<glm::vec3> points;
            std::vector<float> arcLength;
            float totalLength = 0.0f;
        };

        // sampleSplineCurve() is CHORD-parameterised (SplineSampling.hpp:66 derives its step count
        // from |p2-p1|), so its samples are not evenly spaced and its index is not proportional to
        // distance. Redistribute them at an exactly uniform arc-length spacing so cross-sections are
        // even and the V coordinate can be driven off real metres. The input is dense (the spline
        // tool samples at 0.5 m), so interpolating linearly between adjacent input samples costs
        // O(step^2 * curvature).
        [[nodiscard]] inline ResampledCurve resampleByArcLength(const std::vector<glm::vec3>& centerline,
                                                                float spacing)
        {
            ResampledCurve out;
            if (centerline.size() < 2 || spacing <= 0.0f)
                return out;

            std::vector<float> cum(centerline.size(), 0.0f);
            for (size_t i = 1; i < centerline.size(); ++i)
                cum[i] = cum[i - 1] + glm::length(centerline[i] - centerline[i - 1]);

            out.totalLength = cum.back();
            if (out.totalLength <= 0.0f)
                return out;

            const auto segments = std::max<uint32_t>(
                1u, static_cast<uint32_t>(std::lround(out.totalLength / spacing)));
            const float step = out.totalLength / static_cast<float>(segments);

            out.points.reserve(segments + 1);
            out.arcLength.reserve(segments + 1);

            size_t cursor = 0;
            for (uint32_t r = 0; r <= segments; ++r)
            {
                const float s = (r == segments) ? out.totalLength : static_cast<float>(r) * step;
                while (cursor + 2 < centerline.size() && cum[cursor + 1] < s)
                    ++cursor;

                const float segLen = cum[cursor + 1] - cum[cursor];
                const float t = (segLen > 0.0f) ? glm::clamp((s - cum[cursor]) / segLen, 0.0f, 1.0f) : 0.0f;
                out.points.push_back(glm::mix(centerline[cursor], centerline[cursor + 1], t));
                out.arcLength.push_back(s);
            }
            return out;
        }

        // Unit XZ tangent per ring by central difference, forward/backward at the ends. Degenerate
        // steps reuse the last valid direction (+X if there has never been one).
        [[nodiscard]] inline std::vector<glm::vec2> computeTangents(const std::vector<glm::vec3>& pts)
        {
            const size_t n = pts.size();
            std::vector<glm::vec2> tangents(n, glm::vec2(1.0f, 0.0f));
            glm::vec2 last(1.0f, 0.0f);
            for (size_t i = 0; i < n; ++i)
            {
                const glm::vec3& prev = pts[(i == 0) ? 0 : i - 1];
                const glm::vec3& next = pts[(i + 1 < n) ? i + 1 : n - 1];
                const glm::vec2 d(next.x - prev.x, next.z - prev.z);
                const float len = glm::length(d);
                if (len > 1e-5f)
                    last = d / len;
                tangents[i] = last;
            }
            return tangents;
        }

        // right = (-t.z, 0, t.x) — already unit and exactly perpendicular to a unit XZ tangent.
        // This is the SAME perpendicular the spline tool's corridor preview draws
        // (SplineToolPanel.cpp:192-193), so the mesh lands on the white preview lines, and it is a
        // world-up frame, which is inherently twist-free (no parallel transport, no Frenet flips).
        // Keeping it horizontal also measures width IN PLAN, matching the sculpt corridor, which
        // projects onto glm::vec2 (TerrainServiceHandlers.cpp:464-483) — a 3D frame would make the
        // mesh overhang the flattened corridor on grades.
        [[nodiscard]] inline glm::vec3 rightFromTangent(const glm::vec2& t) noexcept
        {
            return glm::vec3(-t.y, 0.0f, t.x);
        }

        struct CurvatureSample
        {
            float radius = 0.0f; // 0 = locally straight
            float sign = 0.0f;   // +1 when the curve rotates toward +right, which is the INNER side
        };

        // Circumradius of the triangle through three consecutive samples: R = abc / (4 * Area),
        // with 2 * Area = |cross|.
        //
        // Deliberately NOT a finite-difference turn angle. Resampled points sit on chords of the
        // input polyline, so a turn-angle estimate jitters by a few percent ring to ring; the corner
        // clamp below turns that jitter into a radial wobble larger than the inner edge's forward
        // advance, and the pinched edge folds back on itself. The circumradius is EXACT for samples
        // on a circular arc no matter how they are spaced, so the clamp comes out stable.
        [[nodiscard]] inline CurvatureSample curvatureAt(const glm::vec3& prev, const glm::vec3& cur,
                                                         const glm::vec3& next)
        {
            const glm::vec2 a(cur.x - prev.x, cur.z - prev.z);
            const glm::vec2 b(next.x - cur.x, next.z - cur.z);
            const glm::vec2 c(next.x - prev.x, next.z - prev.z);

            const float la = glm::length(a);
            const float lb = glm::length(b);
            const float lc = glm::length(c);
            const float cross = a.x * b.y - a.y * b.x;

            if (la < 1e-6f || lb < 1e-6f || lc < 1e-6f || std::abs(cross) < 1e-9f)
                return {};

            return {(la * lb * lc) / (2.0f * std::abs(cross)), (cross > 0.0f) ? 1.0f : -1.0f};
        }

        [[nodiscard]] inline glm::ivec2 tileOf(const glm::vec3& p, float worldTileSize)
        {
            return glm::ivec2(static_cast<int32_t>(std::floor(p.x / worldTileSize)),
                              static_cast<int32_t>(std::floor(p.z / worldTileSize)));
        }

        // Emits one LOD level from a subset of rings. Vertices are written relative to `origin`.
        // Winding is (i,j) (i,j+1) (i+1,j) / (i,j+1) (i+1,j+1) (i+1,j): with tangent +X and
        // right +Z that is (0,h,0) (0,h,w) (d,h,0), whose right-hand-rule normal is +Y —
        // structurally the terrain's own "CCW winding for Vulkan front-face"
        // (TerrainTileGenerator.cpp:280-282), against cullMode=eBack / frontFace=eCounterClockwise
        // (StaticMeshPipelineSetup.cpp:484-485).
        [[nodiscard]] inline resource::LODLevel buildLODLevel(const std::vector<uint32_t>& rings,
                                                              uint32_t columnCount,
                                                              const std::vector<glm::vec3>& worldPos,
                                                              const std::vector<float>& arcLength,
                                                              const RoadProfile& profile,
                                                              const glm::vec3& origin,
                                                              float arcLengthBias)
        {
            resource::LODLevel level;
            if (rings.size() < 2 || columnCount < 2)
                return level;

            const auto ringCount = static_cast<uint32_t>(rings.size());
            const float tilingV = (profile.uvTilingV > 0.0f) ? profile.uvTilingV : 1.0f;

            level.vertices.resize(static_cast<size_t>(ringCount) * columnCount);
            for (uint32_t p = 0; p < ringCount; ++p)
            {
                const uint32_t r = rings[p];
                const float v = (arcLength[r] - arcLengthBias) / tilingV;
                for (uint32_t c = 0; c < columnCount; ++c)
                {
                    resource::Vertex& vert = level.vertices[static_cast<size_t>(p) * columnCount + c];
                    vert.position = worldPos[static_cast<size_t>(r) * columnCount + c] - origin;
                    vert.normal = glm::vec3(0.0f);
                    vert.texCoords = glm::vec2(profile.columns[c].u * profile.uvTilingU, v);
                }
            }

            level.indices.reserve(static_cast<size_t>(ringCount - 1) * (columnCount - 1) * 6);
            for (uint32_t p = 0; p + 1 < ringCount; ++p)
            {
                for (uint32_t c = 0; c + 1 < columnCount; ++c)
                {
                    const uint32_t i00 = p * columnCount + c;
                    const uint32_t i01 = i00 + 1;
                    const uint32_t i10 = (p + 1) * columnCount + c;
                    const uint32_t i11 = i10 + 1;

                    level.indices.push_back(i00);
                    level.indices.push_back(i01);
                    level.indices.push_back(i10);

                    level.indices.push_back(i01);
                    level.indices.push_back(i11);
                    level.indices.push_back(i10);
                }
            }

            // Area-weighted face-normal accumulation. Computed per LOD because ring decimation
            // changes the surface, so LOD normals must not be inherited from LOD0.
            for (size_t i = 0; i + 2 < level.indices.size(); i += 3)
            {
                const uint32_t a = level.indices[i];
                const uint32_t b = level.indices[i + 1];
                const uint32_t c = level.indices[i + 2];
                const glm::vec3 faceNormal = glm::cross(level.vertices[b].position - level.vertices[a].position,
                                                        level.vertices[c].position - level.vertices[a].position);
                level.vertices[a].normal += faceNormal;
                level.vertices[b].normal += faceNormal;
                level.vertices[c].normal += faceNormal;
            }
            for (resource::Vertex& vert : level.vertices)
            {
                const float len = glm::length(vert.normal);
                vert.normal = (len > 1e-8f) ? (vert.normal / len) : glm::vec3(0.0f, 1.0f, 0.0f);
            }

            return level;
        }
    }

    // Builds the road ribbon for one spline.
    //
    // `centerline`     the spline samples (pass terrain::sampleSplineCurve's output verbatim, so the
    //                  mesh follows exactly the curve the sculpt flattened the terrain to).
    // `worldTileSize`  terrain tile size, used to chunk the road at tile boundaries.
    // `heightFn`       terrain surface height at a world XZ (see the file header).
    [[nodiscard]] inline RoadMeshData buildRoadMesh(const std::vector<glm::vec3>& centerline,
                                                    const RoadProfile& profile,
                                                    float worldTileSize,
                                                    const RoadHeightFn& heightFn)
    {
        RoadMeshData data;
        if (centerline.size() < 2 || profile.columns.size() < 2 || worldTileSize <= 0.0f || !heightFn)
            return data;

        const roaddetail::ResampledCurve curve =
            roaddetail::resampleByArcLength(centerline, profile.ringSpacing);
        if (curve.points.size() < 2)
            return data;

        data.totalLength = curve.totalLength;

        const auto ringCount = static_cast<uint32_t>(curve.points.size());
        const auto columnCount = static_cast<uint32_t>(profile.columns.size());
        const std::vector<glm::vec2> tangents = roaddetail::computeTangents(curve.points);

        std::vector<glm::vec3> worldPos(static_cast<size_t>(ringCount) * columnCount);
        std::vector<glm::ivec2> ringTile(ringCount);

        // ---- Corner guard -------------------------------------------------------------------
        // Where the turn radius drops below a column's offset the inner edge folds through the
        // centre of the turn and bowties. Pinch the inner side instead. Computed as its own pass
        // because the limit has to be REGULARISED before it is applied.
        constexpr float kInnerMargin = 0.9f;   // leave a tenth of the radius so the pinched edge
                                               // keeps advancing forward instead of stalling
        constexpr float kMaxInnerSlope = 0.5f; // metres of lateral movement per metre of road
        constexpr float kNoLimit = std::numeric_limits<float>::max();

        std::vector<float> innerLimit(ringCount, kNoLimit);
        std::vector<float> turnSign(ringCount, 0.0f);
        float minRadius = kNoLimit;

        for (uint32_t r = 1; r + 1 < ringCount; ++r)
        {
            const roaddetail::CurvatureSample k =
                roaddetail::curvatureAt(curve.points[r - 1], curve.points[r], curve.points[r + 1]);
            if (k.radius > 0.0f)
            {
                minRadius = std::min(minRadius, k.radius);
                innerLimit[r] = k.radius * kInnerMargin;
                turnSign[r] = k.sign;
            }
        }

        // The first and last rings have no three-point stencil. Leaving them unclamped puts their
        // inner columns PAST the centre of the turn — on a radius-2 hairpin with a 4 m half-width
        // the inner edge lands at radius -2 — and the end quad inverts. Inherit the neighbour.
        if (ringCount >= 3)
        {
            innerLimit[0] = innerLimit[1];
            turnSign[0] = turnSign[1];
            innerLimit[ringCount - 1] = innerLimit[ringCount - 2];
            turnSign[ringCount - 1] = turnSign[ringCount - 2];
        }

        // Slope-limit the clamp in both directions so a tight corner's pinch bleeds into its
        // approach. Without this, a straight run entering a hairpin steps the inner edge sideways
        // by metres in a single ring — sideways motion larger than the forward advance, which is
        // exactly an inversion. Only ever LOWERS a limit, so it can pinch more but never fold.
        // The sign is carried along, but never across an inflection: an S-curve's two arcs pinch
        // opposite sides, and at the inflection itself the radius is large enough not to bind.
        for (uint32_t r = 1; r < ringCount; ++r)
        {
            const float ds = curve.arcLength[r] - curve.arcLength[r - 1];
            const float bound = innerLimit[r - 1] + kMaxInnerSlope * ds;
            if (bound < innerLimit[r] && (turnSign[r] == 0.0f || turnSign[r] == turnSign[r - 1]))
            {
                innerLimit[r] = bound;
                turnSign[r] = turnSign[r - 1];
            }
        }
        for (uint32_t r = ringCount - 1; r > 0; --r)
        {
            const float ds = curve.arcLength[r] - curve.arcLength[r - 1];
            const float bound = innerLimit[r] + kMaxInnerSlope * ds;
            if (bound < innerLimit[r - 1] && (turnSign[r - 1] == 0.0f || turnSign[r - 1] == turnSign[r]))
            {
                innerLimit[r - 1] = bound;
                turnSign[r - 1] = turnSign[r];
            }
        }

        for (uint32_t r = 0; r < ringCount; ++r)
        {
            const glm::vec3& center = curve.points[r];
            const glm::vec3 right = roaddetail::rightFromTangent(tangents[r]);
            ringTile[r] = roaddetail::tileOf(center, worldTileSize);

            const float centerHeight = heightFn(center.x, center.z);

            for (uint32_t c = 0; c < columnCount; ++c)
            {
                const RoadProfileColumn& column = profile.columns[c];

                float offset = column.offset;
                if (turnSign[r] > 0.0f && offset > innerLimit[r])
                {
                    offset = innerLimit[r];
                    data.clamped = true;
                }
                else if (turnSign[r] < 0.0f && offset < -innerLimit[r])
                {
                    offset = -innerLimit[r];
                    data.clamped = true;
                }

                const glm::vec3 p = center + right * offset;
                const float terrainHeight = heightFn(p.x, p.z);
                const float localHeight = profile.flatCrossSection ? centerHeight : terrainHeight;
                const float y = glm::mix(localHeight + column.heightOffset, terrainHeight, column.terrainBlend)
                              + profile.zOffset;

                worldPos[static_cast<size_t>(r) * columnCount + c] = glm::vec3(p.x, y, p.z);
            }
        }

        data.minTurnRadius = (minRadius == kNoLimit) ? 0.0f : minRadius;

        // Chunk at tile boundaries. Chunk k spans rings [start[k] .. start[k+1]] INCLUSIVE, so
        // consecutive chunks share their boundary ring and every quad belongs to exactly one
        // chunk — no gap, no overlap.
        const float minChunkLength = std::max(0.0f, profile.chunkMinTileFraction) * worldTileSize;
        std::vector<uint32_t> starts{0};
        for (uint32_t r = 1; r + 1 < ringCount; ++r)
        {
            if (ringTile[r] != ringTile[r - 1]
                && (curve.arcLength[r] - curve.arcLength[starts.back()]) >= minChunkLength)
                starts.push_back(r);
        }
        // A trailing runt is merged backwards rather than shipped as its own draw.
        if (starts.size() > 1
            && (curve.totalLength - curve.arcLength[starts.back()]) < minChunkLength)
            starts.pop_back();

        data.chunks.reserve(starts.size());
        for (size_t k = 0; k < starts.size(); ++k)
        {
            const uint32_t lo = starts[k];
            const uint32_t hi = (k + 1 < starts.size()) ? starts[k + 1] : (ringCount - 1);
            if (hi <= lo)
                continue;

            RoadChunk chunk;
            chunk.tile = ringTile[lo];
            chunk.origin = glm::vec3(static_cast<float>(chunk.tile.x) * worldTileSize, 0.0f,
                                     static_cast<float>(chunk.tile.y) * worldTileSize);
            chunk.ringCount = hi - lo + 1;

            // UVs are stored as float16 in the .vfMesh (VertexQuantization.hpp:138-139), whose ULP
            // grows with magnitude — a kilometre of road at 8 m tiling would reach V = 125, where
            // fp16 resolves ~0.06 of a repeat and the texture visibly swims. Rebase each chunk's V
            // by a WHOLE number of repeats: V restarts near 0 per chunk, yet the mapping is
            // bit-for-bit continuous across the shared boundary ring, because a wrapped sampler
            // cannot tell V from V - k for integer k.
            const float tilingV = (profile.uvTilingV > 0.0f) ? profile.uvTilingV : 1.0f;
            const float arcLengthBias = std::floor(curve.arcLength[lo] / tilingV) * tilingV;

            // Topology-preserving LOD chain: LOD k keeps every 2^k-th ring and always both ends,
            // and keeps every profile column. Unlike meshopt_simplifySloppy (MeshLODGenerator.cpp:280,
            // which ignores borders and mangles a thin ribbon) this keeps the silhouette exact —
            // which matters because the physics collider reads LOD 2, not LOD 0
            // (PhysicsShapeFactory.cpp:168,222).
            for (uint32_t lod = 0; lod < resource::LOD_LEVEL_COUNT; ++lod)
            {
                const uint32_t stride = 1u << lod;
                std::vector<uint32_t> rings;
                rings.reserve(chunk.ringCount / stride + 2);
                for (uint32_t r = lo; r < hi; r += stride)
                    rings.push_back(r);
                rings.push_back(hi);

                chunk.lods[lod] = roaddetail::buildLODLevel(rings, columnCount, worldPos,
                                                            curve.arcLength, profile, chunk.origin,
                                                            arcLengthBias);
            }

            data.chunks.push_back(std::move(chunk));
        }

        data.valid = !data.chunks.empty();
        return data;
    }
}
