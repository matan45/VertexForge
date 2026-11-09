#pragma once
#include "VoxelGrid.hpp"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// A baked volume on disk (VK-1660): one .vfVolNav per volume entity, read through the VFS. A fixed little-endian
// header - magic "VNVO", version 2, origin, voxelSize, dims, clearance, connectivity, navigable count, payload
// byte count - followed by run-length data: alternating runs of blocked and navigable voxels in index order
// (the first run is blocked, possibly empty), each run length a LEB128 varint.
namespace volumetric
{
    inline constexpr uint32_t kVolumeFileVersion = 2;

    std::vector<uint8_t> encodeVolume(const VoxelGrid& grid);

    // nullopt with `error` set on: a short or wrong-magic header, another version, non-positive or non-finite
    // dimensions / voxel size, more than kMaxVoxels voxels, a payload size that differs from the header, runs that
    // do not add up to the voxel count or overrun it, or a navigable count that does not match.
    std::optional<VoxelGrid> decodeVolume(std::span<const uint8_t> bytes, std::string& error);
}
