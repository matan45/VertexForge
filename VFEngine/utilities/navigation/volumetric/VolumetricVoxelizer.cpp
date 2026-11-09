#include "VolumetricVoxelizer.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <limits>

namespace volumetric
{
    namespace
    {
        // ceil(extent / voxelSize) tolerates this much float noise (in voxels) before adding a voxel.
        constexpr double kDimensionTolerance = 1.0e-4;

        bool isFinite(const glm::vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        // The grid being baked: origin, voxel size and dimensions, with the index math the stages share.
        struct GridFrame
        {
            glm::vec3 origin{0.0f};
            float voxelSize = 1.0f;
            glm::ivec3 dims{0};

            int64_t index(int x, int y, int z) const
            {
                return static_cast<int64_t>(x) +
                       static_cast<int64_t>(dims.x) * (static_cast<int64_t>(y) + static_cast<int64_t>(dims.y) * static_cast<int64_t>(z));
            }

            glm::vec3 voxelMin(int x, int y, int z) const
            {
                return origin + glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) * voxelSize;
            }

            // The voxel range a world box overlaps, clamped to the grid; false when it misses the grid.
            bool voxelRange(const glm::vec3& boxMin, const glm::vec3& boxMax, glm::ivec3& lo, glm::ivec3& hi) const
            {
                if (!isFinite(boxMin) || !isFinite(boxMax))
                    return false;
                for (int axis = 0; axis < 3; ++axis)
                {
                    // Clamped in float before the cast, so a far-away box never overflows the int.
                    const float maxIndex = static_cast<float>(dims[axis] - 1);
                    const float low = std::floor((boxMin[axis] - origin[axis]) / voxelSize);
                    const float high = std::floor((boxMax[axis] - origin[axis]) / voxelSize);
                    if (high < 0.0f || low > maxIndex)
                        return false;
                    lo[axis] = static_cast<int>(std::clamp(low, 0.0f, maxIndex));
                    hi[axis] = static_cast<int>(std::clamp(high, 0.0f, maxIndex));
                }
                return true;
            }
        };

        // Separating-axis test of a triangle against a box (Akenine-Moller): the box's three axes, the triangle
        // normal and the nine edge x box-axis cross products. `half` is the box half extent.
        bool axisSeparates(const glm::vec3& axis, const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                           const glm::vec3& half)
        {
            const float p0 = glm::dot(v0, axis);
            const float p1 = glm::dot(v1, axis);
            const float p2 = glm::dot(v2, axis);
            const float radius = half.x * std::abs(axis.x) + half.y * std::abs(axis.y) + half.z * std::abs(axis.z);
            return std::min({p0, p1, p2}) > radius || std::max({p0, p1, p2}) < -radius;
        }

        bool triangleOverlapsBox(const glm::vec3& center, const glm::vec3& half, const glm::vec3& a,
                                 const glm::vec3& b, const glm::vec3& c)
        {
            const glm::vec3 v0 = a - center;
            const glm::vec3 v1 = b - center;
            const glm::vec3 v2 = c - center;

            for (int axis = 0; axis < 3; ++axis)
            {
                if (std::min({v0[axis], v1[axis], v2[axis]}) > half[axis] ||
                    std::max({v0[axis], v1[axis], v2[axis]}) < -half[axis])
                    return false;
            }

            const glm::vec3 e0 = v1 - v0;
            const glm::vec3 e1 = v2 - v1;
            const glm::vec3 e2 = v0 - v2;
            if (axisSeparates(glm::cross(e0, e1), v0, v1, v2, half))
                return false;

            const glm::vec3 boxAxes[3] = {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
            const glm::vec3 edges[3] = {e0, e1, e2};
            for (const glm::vec3& edge : edges)
            {
                for (const glm::vec3& boxAxis : boxAxes)
                {
                    // A zero axis (an edge parallel to the box axis) never separates.
                    if (axisSeparates(glm::cross(edge, boxAxis), v0, v1, v2, half))
                        return false;
                }
            }
            return true;
        }

        // Marks every voxel the triangle touches. Only the voxels the triangle's PLANE crosses in each column
        // along the normal's dominant axis are SAT-tested, so a large triangle costs its area, not its AABB.
        void rasterizeTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const GridFrame& frame,
                               std::vector<uint8_t>& solid)
        {
            if (!isFinite(a) || !isFinite(b) || !isFinite(c))
                return;

            const glm::vec3 normal = glm::cross(b - a, c - a);
            if (!(glm::dot(normal, normal) > 0.0f))
                return; // degenerate: no surface

            glm::ivec3 lo{0};
            glm::ivec3 hi{0};
            if (!frame.voxelRange(glm::min(a, glm::min(b, c)), glm::max(a, glm::max(b, c)), lo, hi))
                return;

            int w = 0;
            if (std::abs(normal.y) > std::abs(normal[w])) w = 1;
            if (std::abs(normal.z) > std::abs(normal[w])) w = 2;
            const int u = (w + 1) % 3;
            const int v = (w + 2) % 3;

            const float vs = frame.voxelSize;
            const glm::vec3 half(vs * 0.5f * (1.0f + 1.0e-4f)); // a hair larger: a triangle on a face marks it
            const float planeD = glm::dot(normal, a);
            const float margin = vs * 1.0e-3f;

            glm::ivec3 coord{0};
            for (int iu = lo[u]; iu <= hi[u]; ++iu)
            {
                const float u0 = frame.origin[u] + static_cast<float>(iu) * vs;
                for (int iv = lo[v]; iv <= hi[v]; ++iv)
                {
                    const float v0 = frame.origin[v] + static_cast<float>(iv) * vs;

                    // The plane's w over the column's footprint is extreme at its corners.
                    float wMin = std::numeric_limits<float>::infinity();
                    float wMax = -std::numeric_limits<float>::infinity();
                    for (int corner = 0; corner < 4; ++corner)
                    {
                        const float cu = u0 + ((corner & 1) ? vs : 0.0f);
                        const float cv = v0 + ((corner & 2) ? vs : 0.0f);
                        const float cw = (planeD - normal[u] * cu - normal[v] * cv) / normal[w];
                        wMin = std::min(wMin, cw);
                        wMax = std::max(wMax, cw);
                    }

                    const float maxIndex = static_cast<float>(hi[w]);
                    const float minIndex = static_cast<float>(lo[w]);
                    const float first = std::floor((wMin - margin - frame.origin[w]) / vs);
                    const float last = std::floor((wMax + margin - frame.origin[w]) / vs);
                    if (!(last >= minIndex) || !(first <= maxIndex))
                        continue;
                    const int iw0 = static_cast<int>(std::clamp(first, minIndex, maxIndex));
                    const int iw1 = static_cast<int>(std::clamp(last, minIndex, maxIndex));

                    coord[u] = iu;
                    coord[v] = iv;
                    for (int iw = iw0; iw <= iw1; ++iw)
                    {
                        coord[w] = iw;
                        const int64_t index = frame.index(coord.x, coord.y, coord.z);
                        if (solid[static_cast<size_t>(index)] != 0)
                            continue;
                        const glm::vec3 center = frame.voxelMin(coord.x, coord.y, coord.z) + glm::vec3(vs * 0.5f);
                        if (triangleOverlapsBox(center, half, a, b, c))
                            solid[static_cast<size_t>(index)] = 1;
                    }
                }
            }
        }

        // The highest terrain height over each (x, z) column center, from the terrain triangles whose XZ
        // projection covers it.
        void recordTerrainHeights(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const GridFrame& frame,
                                  std::vector<float>& columnHeights)
        {
            if (!isFinite(a) || !isFinite(b) || !isFinite(c))
                return;

            const float area = (b.x - a.x) * (c.z - a.z) - (c.x - a.x) * (b.z - a.z);
            if (std::abs(area) < 1.0e-12f)
                return; // vertical: no height over a column

            const float vs = frame.voxelSize;
            const float minX = std::min({a.x, b.x, c.x});
            const float maxX = std::max({a.x, b.x, c.x});
            const float minZ = std::min({a.z, b.z, c.z});
            const float maxZ = std::max({a.z, b.z, c.z});

            // Columns whose centers can lie inside: center x = origin.x + (i + 0.5) * vs.
            const float maxXIndex = static_cast<float>(frame.dims.x - 1);
            const float maxZIndex = static_cast<float>(frame.dims.z - 1);
            const float firstX = std::ceil((minX - frame.origin.x) / vs - 0.5f);
            const float lastX = std::floor((maxX - frame.origin.x) / vs - 0.5f);
            const float firstZ = std::ceil((minZ - frame.origin.z) / vs - 0.5f);
            const float lastZ = std::floor((maxZ - frame.origin.z) / vs - 0.5f);
            if (lastX < 0.0f || firstX > maxXIndex || lastZ < 0.0f || firstZ > maxZIndex)
                return;
            const int ix0 = static_cast<int>(std::clamp(firstX, 0.0f, maxXIndex));
            const int ix1 = static_cast<int>(std::clamp(lastX, 0.0f, maxXIndex));
            const int iz0 = static_cast<int>(std::clamp(firstZ, 0.0f, maxZIndex));
            const int iz1 = static_cast<int>(std::clamp(lastZ, 0.0f, maxZIndex));

            const float tolerance = -1.0e-5f * std::abs(area);
            for (int iz = iz0; iz <= iz1; ++iz)
            {
                const float pz = frame.origin.z + (static_cast<float>(iz) + 0.5f) * vs;
                for (int ix = ix0; ix <= ix1; ++ix)
                {
                    const float px = frame.origin.x + (static_cast<float>(ix) + 0.5f) * vs;
                    // Barycentric weights in XZ (signed with the triangle's winding).
                    float w0 = (b.x - px) * (c.z - pz) - (c.x - px) * (b.z - pz);
                    float w1 = (c.x - px) * (a.z - pz) - (a.x - px) * (c.z - pz);
                    float w2 = (a.x - px) * (b.z - pz) - (b.x - px) * (a.z - pz);
                    if (area < 0.0f)
                    {
                        w0 = -w0;
                        w1 = -w1;
                        w2 = -w2;
                    }
                    if (w0 < tolerance || w1 < tolerance || w2 < tolerance)
                        continue;
                    const float total = std::abs(area);
                    const float height = (w0 * a.y + w1 * b.y + w2 * c.y) / total;
                    float& column = columnHeights[static_cast<size_t>(ix + static_cast<int64_t>(frame.dims.x) * iz)];
                    column = std::max(column, height);
                }
            }
        }

        float distanceSquaredToBox(const glm::vec3& point, const glm::vec3& boxMin, const glm::vec3& boxMax)
        {
            const glm::vec3 closest = glm::clamp(point, boxMin, boxMax);
            const glm::vec3 delta = point - closest;
            return glm::dot(delta, delta);
        }

        float distanceSquaredToSegment(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b)
        {
            const glm::vec3 ab = b - a;
            const float lengthSquared = glm::dot(ab, ab);
            const float t = lengthSquared > 0.0f ? std::clamp(glm::dot(point - a, ab) / lengthSquared, 0.0f, 1.0f) : 0.0f;
            const glm::vec3 delta = point - (a + ab * t);
            return glm::dot(delta, delta);
        }

        void rasterizeSphere(const VoxelSphere& sphere, const GridFrame& frame, std::vector<uint8_t>& solid)
        {
            if (!isFinite(sphere.center) || !std::isfinite(sphere.radius) || !(sphere.radius > 0.0f))
                return;

            glm::ivec3 lo{0};
            glm::ivec3 hi{0};
            if (!frame.voxelRange(sphere.center - glm::vec3(sphere.radius), sphere.center + glm::vec3(sphere.radius), lo, hi))
                return;

            const float radiusSquared = sphere.radius * sphere.radius;
            const glm::vec3 size(frame.voxelSize);
            for (int z = lo.z; z <= hi.z; ++z)
                for (int y = lo.y; y <= hi.y; ++y)
                    for (int x = lo.x; x <= hi.x; ++x)
                    {
                        const glm::vec3 boxMin = frame.voxelMin(x, y, z);
                        if (distanceSquaredToBox(sphere.center, boxMin, boxMin + size) <= radiusSquared)
                            solid[static_cast<size_t>(frame.index(x, y, z))] = 1;
                    }
        }

        // Exact (to a 1e-6 fraction of the segment): the squared distance from the box to the segment is convex
        // in the segment parameter, so a ternary search finds its minimum.
        void rasterizeCapsule(const VoxelCapsule& capsule, const GridFrame& frame, std::vector<uint8_t>& solid)
        {
            if (!isFinite(capsule.a) || !isFinite(capsule.b) || !std::isfinite(capsule.radius) || !(capsule.radius > 0.0f))
                return;

            const glm::vec3 reach(capsule.radius);
            glm::ivec3 lo{0};
            glm::ivec3 hi{0};
            if (!frame.voxelRange(glm::min(capsule.a, capsule.b) - reach, glm::max(capsule.a, capsule.b) + reach, lo, hi))
                return;

            const float radius = capsule.radius;
            const float radiusSquared = radius * radius;
            const float halfDiagonal = frame.voxelSize * 0.8660254f; // sqrt(3) / 2
            const glm::vec3 size(frame.voxelSize);
            const glm::vec3 ab = capsule.b - capsule.a;

            for (int z = lo.z; z <= hi.z; ++z)
                for (int y = lo.y; y <= hi.y; ++y)
                    for (int x = lo.x; x <= hi.x; ++x)
                    {
                        const glm::vec3 boxMin = frame.voxelMin(x, y, z);
                        const glm::vec3 boxMax = boxMin + size;
                        const glm::vec3 center = boxMin + size * 0.5f;
                        const float centerDistance = std::sqrt(distanceSquaredToSegment(center, capsule.a, capsule.b));
                        if (centerDistance > radius + halfDiagonal)
                            continue; // too far for any part of the box
                        bool hit = centerDistance <= radius;
                        if (!hit)
                        {
                            float low = 0.0f;
                            float high = 1.0f;
                            for (int iteration = 0; iteration < 40; ++iteration)
                            {
                                const float m1 = low + (high - low) / 3.0f;
                                const float m2 = high - (high - low) / 3.0f;
                                if (distanceSquaredToBox(capsule.a + ab * m1, boxMin, boxMax) <=
                                    distanceSquaredToBox(capsule.a + ab * m2, boxMin, boxMax))
                                    high = m2;
                                else
                                    low = m1;
                            }
                            hit = distanceSquaredToBox(capsule.a + ab * ((low + high) * 0.5f), boxMin, boxMax) <= radiusSquared;
                        }
                        if (hit)
                            solid[static_cast<size_t>(frame.index(x, y, z))] = 1;
                    }
        }

        // out[i] = any of in[i - r .. i + r] along one axis (a sliding count per line).
        void dilateLines(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, const GridFrame& frame, int axis, int radius)
        {
            const glm::ivec3& dims = frame.dims;
            const int length = dims[axis];
            const int64_t stride = axis == 0 ? 1 : axis == 1 ? static_cast<int64_t>(dims.x)
                                                            : static_cast<int64_t>(dims.x) * static_cast<int64_t>(dims.y);
            const int other1 = axis == 0 ? 1 : 0;
            const int other2 = axis == 2 ? 1 : 2;

            for (int b = 0; b < dims[other2]; ++b)
            {
                for (int a = 0; a < dims[other1]; ++a)
                {
                    glm::ivec3 start{0};
                    start[other1] = a;
                    start[other2] = b;
                    const int64_t base = frame.index(start.x, start.y, start.z);

                    int count = 0;
                    for (int k = 0; k <= std::min(radius, length - 1); ++k)
                        count += in[static_cast<size_t>(base + stride * k)];
                    for (int i = 0; i < length; ++i)
                    {
                        out[static_cast<size_t>(base + stride * i)] = count > 0 ? uint8_t{1} : uint8_t{0};
                        if (i - radius >= 0)
                            count -= in[static_cast<size_t>(base + stride * (i - radius))];
                        if (i + radius + 1 < length)
                            count += in[static_cast<size_t>(base + stride * (i + radius + 1))];
                    }
                }
            }
        }

        VolumetricBakeResult failed(std::string error)
        {
            VolumetricBakeResult result;
            result.error = std::move(error);
            return result;
        }

        VolumetricBakeResult cancelledResult()
        {
            VolumetricBakeResult result;
            result.cancelled = true;
            result.error = "the bake was cancelled";
            return result;
        }
    }

    glm::ivec3 gridDimensionsFor(const glm::vec3& boundsMin, const glm::vec3& boundsMax, float voxelSize)
    {
        if (!std::isfinite(voxelSize) || !(voxelSize > 0.0f) || !isFinite(boundsMin) || !isFinite(boundsMax))
            return glm::ivec3{0};

        glm::ivec3 dims{0};
        for (int axis = 0; axis < 3; ++axis)
        {
            const double extent = static_cast<double>(boundsMax[axis]) - static_cast<double>(boundsMin[axis]);
            if (!(extent > 0.0))
                return glm::ivec3{0};
            const double cells = std::ceil(extent / static_cast<double>(voxelSize) - kDimensionTolerance);
            // A huge count saturates (it is far above kMaxVoxels either way).
            dims[axis] = cells >= static_cast<double>(std::numeric_limits<int32_t>::max())
                ? std::numeric_limits<int32_t>::max()
                : std::max(1, static_cast<int32_t>(cells));
        }
        return dims;
    }

    int64_t voxelCountFor(const glm::vec3& boundsMin, const glm::vec3& boundsMax, float voxelSize)
    {
        const glm::ivec3 dims = gridDimensionsFor(boundsMin, boundsMax, voxelSize);
        if (dims.x <= 0 || dims.y <= 0 || dims.z <= 0)
            return 0;
        const double product = static_cast<double>(dims.x) * static_cast<double>(dims.y) * static_cast<double>(dims.z);
        if (product >= 9.0e18)
            return std::numeric_limits<int64_t>::max();
        return static_cast<int64_t>(dims.x) * static_cast<int64_t>(dims.y) * static_cast<int64_t>(dims.z);
    }

    float minVoxelSizeFor(const glm::vec3& boundsMin, const glm::vec3& boundsMax)
    {
        if (!isFinite(boundsMin) || !isFinite(boundsMax))
            return 0.0f;
        const glm::vec3 extent = boundsMax - boundsMin;
        if (!(extent.x > 0.0f) || !(extent.y > 0.0f) || !(extent.z > 0.0f))
            return 0.0f;

        // The voxel count only falls as the voxel size grows: bisect for the smallest size that fits. At the
        // largest extent every axis has one voxel, so `high` always fits.
        float high = std::max({extent.x, extent.y, extent.z});
        float low = high * 1.0e-9f;
        if (voxelCountFor(boundsMin, boundsMax, low) <= kMaxVoxels)
            return low;
        for (int iteration = 0; iteration < 80; ++iteration)
        {
            const float middle = low + (high - low) * 0.5f;
            if (middle <= low || middle >= high)
                break;
            if (voxelCountFor(boundsMin, boundsMax, middle) <= kMaxVoxels)
                high = middle;
            else
                low = middle;
        }
        return high;
    }

    VolumetricBakeResult voxelize(const VolumetricBakeInput& input, const threading::CancellationToken* cancel,
                                  const std::function<void(float)>& progress)
    {
        const VolumetricBakeSettings& settings = input.settings;
        if (!isFinite(input.boundsMin) || !isFinite(input.boundsMax) || !(input.boundsMin.x < input.boundsMax.x) ||
            !(input.boundsMin.y < input.boundsMax.y) || !(input.boundsMin.z < input.boundsMax.z))
            return failed("the volume's box is empty or not finite");
        if (!std::isfinite(settings.voxelSize) || !(settings.voxelSize > 0.0f))
            return failed(std::format("voxelSize must be a positive number (got {})", settings.voxelSize));
        if (!std::isfinite(settings.clearance) || settings.clearance < 0.0f)
            return failed(std::format("clearance must be zero or more (got {})", settings.clearance));
        if (settings.connectivity != 6 && settings.connectivity != 26)
            return failed(std::format("connectivity must be 6 or 26 (got {})", static_cast<int>(settings.connectivity)));

        const int64_t count = voxelCountFor(input.boundsMin, input.boundsMax, settings.voxelSize);
        if (count <= 0)
            return failed("the volume's box is empty");
        if (count > kMaxVoxels)
        {
            const float minimum = std::ceil(minVoxelSizeFor(input.boundsMin, input.boundsMax) * 100.0f) / 100.0f;
            return failed(std::format("the volume needs {} voxels at voxelSize {} (the limit is {}): raise voxelSize to "
                                      ">= {:.2f} or shrink the volume",
                                      count, settings.voxelSize, kMaxVoxels, minimum));
        }

        float reported = 0.0f;
        auto report = [&](float value)
        {
            value = std::clamp(value, 0.0f, 1.0f);
            if (value > reported)
            {
                reported = value;
                if (progress)
                    progress(value);
            }
        };
        auto isCancelled = [cancel]() { return cancel && cancel->isCancelled(); };

        GridFrame frame;
        frame.origin = input.boundsMin;
        frame.voxelSize = settings.voxelSize;
        frame.dims = gridDimensionsFor(input.boundsMin, input.boundsMax, settings.voxelSize);

        auto grid = std::make_shared<VoxelGrid>(input.boundsMin, frame.dims, settings);
        std::vector<uint8_t>& reached = grid->data(); // 1 = open space reached from the boundary
        std::vector<uint8_t> solid(static_cast<size_t>(count), uint8_t{0});
        report(0.01f);

        // 1. Surfaces: meshes / colliders, terrain (plus its columns), spheres, capsules. 0.01 .. 0.5
        const size_t meshTriangles = input.indices.size() / 3;
        const size_t terrainTriangles = input.terrainIndices.size() / 3;
        const size_t meshVertices = input.vertices.size() / 3;
        const size_t terrainVertices = input.terrainVertices.size() / 3;
        const size_t surfaceWork = std::max<size_t>(1, meshTriangles + terrainTriangles * 2 + input.spheres.size() +
                                                          input.capsules.size());
        size_t surfaceDone = 0;
        auto vertexAt = [](const std::vector<float>& vertices, uint32_t vertex)
        {
            const size_t base = static_cast<size_t>(vertex) * 3;
            return glm::vec3(vertices[base], vertices[base + 1], vertices[base + 2]);
        };
        auto surfaceStep = [&]()
        {
            ++surfaceDone;
            if ((surfaceDone & 1023u) == 0)
            {
                report(0.01f + 0.49f * static_cast<float>(surfaceDone) / static_cast<float>(surfaceWork));
                return !isCancelled();
            }
            return true;
        };

        for (size_t t = 0; t < meshTriangles; ++t)
        {
            const uint32_t i0 = input.indices[t * 3];
            const uint32_t i1 = input.indices[t * 3 + 1];
            const uint32_t i2 = input.indices[t * 3 + 2];
            if (i0 < meshVertices && i1 < meshVertices && i2 < meshVertices)
                rasterizeTriangle(vertexAt(input.vertices, i0), vertexAt(input.vertices, i1), vertexAt(input.vertices, i2),
                                  frame, solid);
            if (!surfaceStep())
                return cancelledResult();
        }

        std::vector<float> columnHeights;
        if (terrainTriangles > 0)
            columnHeights.assign(static_cast<size_t>(frame.dims.x) * static_cast<size_t>(frame.dims.z),
                                 -std::numeric_limits<float>::infinity());
        for (size_t t = 0; t < terrainTriangles; ++t)
        {
            const uint32_t i0 = input.terrainIndices[t * 3];
            const uint32_t i1 = input.terrainIndices[t * 3 + 1];
            const uint32_t i2 = input.terrainIndices[t * 3 + 2];
            if (i0 < terrainVertices && i1 < terrainVertices && i2 < terrainVertices)
            {
                const glm::vec3 a = vertexAt(input.terrainVertices, i0);
                const glm::vec3 b = vertexAt(input.terrainVertices, i1);
                const glm::vec3 c = vertexAt(input.terrainVertices, i2);
                rasterizeTriangle(a, b, c, frame, solid);
                recordTerrainHeights(a, b, c, frame, columnHeights);
            }
            if (!surfaceStep() || !surfaceStep())
                return cancelledResult();
        }
        // Below the terrain surface: every voxel whose center is under the column's height.
        for (int z = 0; z < frame.dims.z && !columnHeights.empty(); ++z)
        {
            for (int x = 0; x < frame.dims.x; ++x)
            {
                const float height = columnHeights[static_cast<size_t>(x + static_cast<int64_t>(frame.dims.x) * z)];
                if (!std::isfinite(height))
                    continue;
                const float below = std::ceil((height - frame.origin.y) / frame.voxelSize - 0.5f);
                const int top = static_cast<int>(std::clamp(below, 0.0f, static_cast<float>(frame.dims.y)));
                for (int y = 0; y < top; ++y)
                    solid[static_cast<size_t>(frame.index(x, y, z))] = 1;
            }
            if (isCancelled())
                return cancelledResult();
        }

        for (const VoxelSphere& sphere : input.spheres)
        {
            rasterizeSphere(sphere, frame, solid);
            if (!surfaceStep())
                return cancelledResult();
        }
        for (const VoxelCapsule& capsule : input.capsules)
        {
            rasterizeCapsule(capsule, frame, solid);
            if (!surfaceStep())
                return cancelledResult();
        }
        if (isCancelled())
            return cancelledResult();
        report(0.5f);

        // 2. Flood fill (6-connected) from the free boundary voxels: open space. 0.5 .. 0.75
        std::deque<int64_t> frontier;
        int64_t visited = 0;
        auto visit = [&](int x, int y, int z)
        {
            const size_t index = static_cast<size_t>(frame.index(x, y, z));
            if (solid[index] == 0 && reached[index] == 0)
            {
                reached[index] = 1;
                frontier.push_back(static_cast<int64_t>(index));
            }
        };
        const glm::ivec3& dims = frame.dims;
        for (int z = 0; z < dims.z; ++z)
            for (int y = 0; y < dims.y; ++y)
            {
                visit(0, y, z);
                visit(dims.x - 1, y, z);
            }
        for (int z = 0; z < dims.z; ++z)
            for (int x = 0; x < dims.x; ++x)
            {
                visit(x, 0, z);
                visit(x, dims.y - 1, z);
            }
        for (int y = 0; y < dims.y; ++y)
            for (int x = 0; x < dims.x; ++x)
            {
                visit(x, y, 0);
                visit(x, y, dims.z - 1);
            }

        const int64_t plane = static_cast<int64_t>(dims.x) * static_cast<int64_t>(dims.y);
        while (!frontier.empty())
        {
            const int64_t index = frontier.front();
            frontier.pop_front();
            const int z = static_cast<int>(index / plane);
            const int64_t rest = index - static_cast<int64_t>(z) * plane;
            const int y = static_cast<int>(rest / dims.x);
            const int x = static_cast<int>(rest - static_cast<int64_t>(y) * dims.x);
            if (x > 0) visit(x - 1, y, z);
            if (x + 1 < dims.x) visit(x + 1, y, z);
            if (y > 0) visit(x, y - 1, z);
            if (y + 1 < dims.y) visit(x, y + 1, z);
            if (z > 0) visit(x, y, z - 1);
            if (z + 1 < dims.z) visit(x, y, z + 1);

            if ((++visited & 65535) == 0)
            {
                report(0.5f + 0.25f * static_cast<float>(static_cast<double>(visited) / static_cast<double>(count)));
                if (isCancelled())
                    return cancelledResult();
            }
        }
        report(0.75f);

        // 3. Clearance: grow the blocked voxels (solid or enclosed) by r voxels, a separable cube kernel. 0.75 .. 1
        const int radius = settings.clearance > 0.0f
            ? static_cast<int>(std::ceil(static_cast<double>(settings.clearance) / static_cast<double>(settings.voxelSize) - 1.0e-6))
            : 0;
        if (radius > 0)
        {
            std::vector<uint8_t>& blocked = solid; // reused: the surfaces are no longer needed
            for (size_t i = 0; i < blocked.size(); ++i)
                blocked[i] = reached[i] != 0 ? uint8_t{0} : uint8_t{1};
            std::vector<uint8_t> grown(static_cast<size_t>(count), uint8_t{0});

            dilateLines(blocked, grown, frame, 0, radius);
            if (isCancelled())
                return cancelledResult();
            report(0.83f);
            dilateLines(grown, blocked, frame, 1, radius);
            if (isCancelled())
                return cancelledResult();
            report(0.91f);
            dilateLines(blocked, grown, frame, 2, radius);
            if (isCancelled())
                return cancelledResult();

            for (size_t i = 0; i < grown.size(); ++i)
                reached[i] = grown[i] != 0 ? uint8_t{0} : uint8_t{1};
        }
        report(1.0f);

        VolumetricBakeResult result;
        result.voxels = count;
        result.navigable = grid->navigableCount();
        result.grid = std::move(grid);
        return result;
    }
}
