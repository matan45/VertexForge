#pragma once
#include "VoxelGrid.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <optional>
#include <vector>

namespace volumetric
{
    enum class VolumePathStatus : uint8_t
    {
        Found,            // the path reaches the (snapped) end
        Partial,          // the end was not reached (node cap, or not connected): the path ends at the reached
                          // voxel nearest to it
        NoVolume,         // no volume is baked
        OutsideVolume,    // the start or the end lies outside every volume
        StartBlocked,     // the start is blocked and no navigable voxel lies within snapRadius
        EndBlocked,       // the same for the end
        DifferentVolumes, // start and end are in volumes, but no single volume holds both
        NoPath            // not one step toward the end is possible
    };

    // Canonical name: "Found", "Partial", ...
    const char* volumePathStatusName(VolumePathStatus status);

    struct PathRequest
    {
        glm::vec3 start{0.0f};
        glm::vec3 end{0.0f};
        float snapRadius = 2.0f;   // a blocked start / end moves to the nearest navigable voxel within this (m)
        uint32_t maxNodes = 32768; // A* expansions before the best partial path is returned
    };

    struct VolumePathResult
    {
        VolumePathStatus status = VolumePathStatus::NoPath;
        // (snapped) start .. (snapped) end, or .. the nearest reached voxel for Partial; smoothed: a waypoint is
        // dropped when its neighbours see each other through navigable voxels. Empty unless hasPath().
        std::vector<glm::vec3> waypoints;
        std::optional<glm::vec3> startSnapped; // set when the start was moved onto a navigable voxel
        std::optional<glm::vec3> endSnapped;
        uint32_t nodesExpanded = 0;
        uint64_t volumeId = 0; // the volume searched (VolumetricNavWorld: its entity UUID)

        bool hasPath() const { return status == VolumePathStatus::Found || status == VolumePathStatus::Partial; }
    };

    // A* over one grid with its connectivity (Euclidean step costs and heuristic). A start / end outside the grid
    // is OutsideVolume. A start and end in the same voxel is Found with [start, end].
    VolumePathResult findPath(const VoxelGrid& grid, const PathRequest& request);

    // `point` when its voxel is navigable, else the nearest navigable voxel center within snapRadius; nullopt when
    // there is none or the point is outside the grid.
    std::optional<glm::vec3> snapToNavigable(const VoxelGrid& grid, const glm::vec3& point, float snapRadius);

    // True when every voxel the segment passes through is navigable (3D DDA); false when it leaves the grid.
    bool hasLineOfSight(const VoxelGrid& grid, const glm::vec3& from, const glm::vec3& to);
}
