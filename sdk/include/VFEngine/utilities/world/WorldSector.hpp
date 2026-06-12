#pragma once

#include "WorldTypes.hpp"
#include "../math/Frustum.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace world
{
    static constexpr std::array<char, 4> SECTOR_MAGIC = {'V', 'F', 'S', 'C'};
    static constexpr uint32_t SECTOR_FORMAT_VERSION = 3;
    static constexpr uint32_t SECTOR_MIN_SUPPORTED_VERSION = 2;
    static constexpr size_t SECTOR_HEADER_SIZE = 44; // 4 + 4 + 4 + 24 + 8

    // v3 TLV section ids (written after the entity blob)
    static constexpr uint32_t SECTOR_SECTION_DATA_LAYERS = 1;

    // Named per-sector binary payloads (gameplay grids, fog-of-war, plugin data)
    using SectorDataLayers = std::unordered_map<std::string, std::vector<uint8_t>>;

    struct SectorFileHeader
    {
        std::array<char, 4> magic = {'V', 'F', 'S', 'C'};
        uint32_t version = SECTOR_FORMAT_VERSION;
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
        std::string hlodFilePath;
        SectorMetadata metadata;
        SectorDataLayers dataLayers; // persisted as v3 sections alongside entities
    };

} // namespace world
