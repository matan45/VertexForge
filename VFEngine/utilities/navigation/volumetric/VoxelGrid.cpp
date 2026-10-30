#include "VoxelGrid.hpp"
#include <algorithm>
#include <cmath>

namespace volumetric
{
    VoxelGrid::VoxelGrid(const glm::vec3& origin, const glm::ivec3& dims, const VolumetricBakeSettings& settings)
        : originPoint(origin),
          bakeSettings(settings)
    {
        // A precondition violation (non-positive or over-cap dims) leaves an empty grid rather than a huge
        // allocation.
        if (dims.x <= 0 || dims.y <= 0 || dims.z <= 0)
            return;
        const int64_t planeCount = static_cast<int64_t>(dims.x) * static_cast<int64_t>(dims.y);
        if (planeCount > kMaxVoxels)
            return;
        const int64_t count = planeCount * static_cast<int64_t>(dims.z);
        if (count > kMaxVoxels)
            return;

        dimensions = dims;
        cells.assign(static_cast<size_t>(count), uint8_t{0});
    }

    glm::vec3 VoxelGrid::worldMax() const
    {
        return originPoint + glm::vec3(dimensions) * bakeSettings.voxelSize;
    }

    bool VoxelGrid::inBounds(const VoxelCoord& c) const
    {
        return c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x < dimensions.x && c.y < dimensions.y && c.z < dimensions.z;
    }

    int64_t VoxelGrid::index(const VoxelCoord& c) const
    {
        return static_cast<int64_t>(c.x) +
               static_cast<int64_t>(dimensions.x) *
                   (static_cast<int64_t>(c.y) + static_cast<int64_t>(dimensions.y) * static_cast<int64_t>(c.z));
    }

    VoxelCoord VoxelGrid::coordOf(int64_t index) const
    {
        const int64_t plane = static_cast<int64_t>(dimensions.x) * static_cast<int64_t>(dimensions.y);
        const int64_t z = index / plane;
        const int64_t rest = index - z * plane;
        const int64_t y = rest / dimensions.x;
        const int64_t x = rest - y * dimensions.x;
        return VoxelCoord{static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z)};
    }

    glm::vec3 VoxelGrid::centerOf(const VoxelCoord& c) const
    {
        return originPoint + (glm::vec3(static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z)) +
                              glm::vec3(0.5f)) * bakeSettings.voxelSize;
    }

    std::optional<VoxelCoord> VoxelGrid::coordAt(const glm::vec3& world) const
    {
        if (cells.empty())
            return std::nullopt;

        // Checked in floating point first: a non-finite or far-away point must never reach the int cast.
        const glm::vec3 local = (world - originPoint) / bakeSettings.voxelSize;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float value = local[axis];
            if (!std::isfinite(value) || value < 0.0f || value >= static_cast<float>(dimensions[axis]))
                return std::nullopt;
        }

        VoxelCoord coord{static_cast<int32_t>(std::floor(local.x)), static_cast<int32_t>(std::floor(local.y)),
                         static_cast<int32_t>(std::floor(local.z))};
        // Float rounding at the max face can still land on dims.
        coord.x = std::min(coord.x, dimensions.x - 1);
        coord.y = std::min(coord.y, dimensions.y - 1);
        coord.z = std::min(coord.z, dimensions.z - 1);
        return coord;
    }

    bool VoxelGrid::isNavigable(const VoxelCoord& c) const
    {
        return inBounds(c) && cells[static_cast<size_t>(index(c))] != 0;
    }

    void VoxelGrid::setNavigable(const VoxelCoord& c, bool navigable)
    {
        if (inBounds(c))
            cells[static_cast<size_t>(index(c))] = navigable ? uint8_t{1} : uint8_t{0};
    }

    int64_t VoxelGrid::navigableCount() const
    {
        return static_cast<int64_t>(std::count_if(cells.begin(), cells.end(), [](uint8_t cell) { return cell != 0; }));
    }

    void VoxelGrid::neighbors(const VoxelCoord& c, std::vector<VoxelCoord>& out) const
    {
        out.clear();
        const bool faceOnly = bakeSettings.connectivity == 6;

        for (int dz = -1; dz <= 1; ++dz)
        {
            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int axes = (dx != 0) + (dy != 0) + (dz != 0);
                    if (axes == 0 || (faceOnly && axes > 1))
                        continue;

                    const VoxelCoord next{c.x + dx, c.y + dy, c.z + dz};
                    if (!isNavigable(next))
                        continue;

                    // No corner cutting: every voxel the step passes - each partial step that moves along a
                    // proper, non-empty subset of the step's axes - must be navigable too.
                    bool clear = true;
                    if (axes > 1)
                    {
                        const int step[3] = {dx, dy, dz};
                        for (int mask = 1; mask < 7 && clear; ++mask)
                        {
                            VoxelCoord partial = c;
                            int moved = 0;
                            bool usable = true;
                            for (int axis = 0; axis < 3; ++axis)
                            {
                                if ((mask & (1 << axis)) == 0)
                                    continue;
                                if (step[axis] == 0)
                                {
                                    usable = false; // not a sub-step of this move
                                    break;
                                }
                                ++moved;
                                if (axis == 0) partial.x += step[axis];
                                else if (axis == 1) partial.y += step[axis];
                                else partial.z += step[axis];
                            }
                            if (!usable || moved == 0 || moved == axes)
                                continue;
                            clear = isNavigable(partial);
                        }
                    }
                    if (clear)
                        out.push_back(next);
                }
            }
        }
    }
}
