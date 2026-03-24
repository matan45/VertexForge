#pragma once

#include "WorldTypes.hpp"
#include "../math/Frustum.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace world
{
    static constexpr std::array<char, 4> SECTOR_MAGIC = {'V', 'F', 'S', 'C'};
    static constexpr uint32_t SECTOR_FORMAT_VERSION = 2;
    static constexpr size_t SECTOR_HEADER_SIZE = 44; // 4 + 4 + 4 + 24 + 8

    struct SectorFileHeader
    {
        std::array<char, 4> magic = {'V', 'F', 'S', 'C'};
        uint32_t version = 2;
        uint32_t entityCount = 0;
        float aabbMinX = 0.0f;
        float aabbMinY = 0.0f;
        float aabbMinZ = 0.0f;
        float aabbMaxX = 0.0f;
        float aabbMaxY = 0.0f;
        float aabbMaxZ = 0.0f;
        uint64_t totalFileSize = 0;
    };

    struct SectorMetadata
    {
        uint32_t entityCount = 0;
        math::AABB bounds;
        uint64_t estimatedMemory = 0;
        bool valid = false;
    };

    struct WorldSector
    {
        SectorCoord coord;
        SectorState state = SectorState::Unloaded;
        std::vector<uint64_t> entityUUIDs;
        bool dirty = false;
        std::string filePath;
        SectorMetadata metadata;
    };

} // namespace world
