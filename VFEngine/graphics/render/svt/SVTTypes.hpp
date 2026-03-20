#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace render::svt
{
    // ---- Configuration ----

    struct SVTConfig
    {
        uint32_t virtualTextureSizeLog2 = 17;   // 131072 virtual texels
        uint32_t tileSizeLog2 = 7;              // 128x128 texels per tile
        uint32_t borderSize = 4;                // Border padding for filtering
        uint32_t physicalTileCount = 2048;      // Tiles per channel cache
        uint32_t tailMipCount = 2;              // Always-resident coarse mips

        size_t maxBytesPerFrame = 4 * 1024 * 1024;   // 4 MB upload budget
        uint32_t maxTilesPerFrame = 16;               // Max tile uploads per frame
        uint32_t feedbackLatencyFrames = 2;           // Frames between GPU write and CPU read
    };

    // ---- Constants ----

    constexpr uint32_t SVT_TILE_SIZE = 128;
    constexpr uint32_t SVT_BORDER_SIZE = 4;
    constexpr uint32_t SVT_PHYSICAL_TILE_SIZE = SVT_TILE_SIZE + 2 * SVT_BORDER_SIZE;  // 136

    // BC7 block = 4x4 texels = 16 bytes
    // 136x136 tile = 34x34 blocks = 1156 blocks * 16 = 18496 bytes per tile per channel
    constexpr uint32_t SVT_BC7_BLOCK_SIZE = 16;
    constexpr uint32_t SVT_PHYSICAL_TILE_BLOCKS = (SVT_PHYSICAL_TILE_SIZE / 4) * (SVT_PHYSICAL_TILE_SIZE / 4);
    constexpr uint32_t SVT_TILE_SIZE_BC7 = SVT_PHYSICAL_TILE_BLOCKS * SVT_BC7_BLOCK_SIZE;

    constexpr uint32_t SVT_MAX_MIP_LEVELS = 11;       // log2(131072/128) + 1
    constexpr uint32_t SVT_INVALID_TILE = 0xFFFFFFFF;

    // ---- SVT Channel Indices ----
    constexpr uint32_t SVT_CHANNEL_ALBEDO = 0;
    constexpr uint32_t SVT_CHANNEL_NORMAL = 1;
    constexpr uint32_t SVT_CHANNEL_ORM = 2;
    constexpr uint32_t SVT_CHANNEL_EMISSION = 3;
    constexpr uint32_t SVT_CHANNEL_HEIGHT = 4;
    constexpr uint32_t SVT_CHANNEL_COUNT = 5;

    // ---- Page Table Entry Layout ----
    //
    // Single uint32 per entry (4 bytes). All 5 texture channels share the same
    // physical tile slot index (loaded/evicted together as a group).
    //
    //   bits [0..11]   physical tile index (0-4095), same for all channels
    //   bit  [12]      valid flag (1 = tile is resident)
    //   bits [13..15]  mip delta (fallback: how many mips coarser than requested)
    //   bits [16..20]  channel presence mask (bit per channel: albedo, normal, ORM, emission, height)
    //   bits [21..31]  reserved

    struct SVTPageTableEntry
    {
        uint32_t packed = 0;

        static SVTPageTableEntry encode(uint32_t physTile, uint32_t mipDelta,
                                        bool valid, uint32_t channelMask = 0x7u)
        {
            SVTPageTableEntry e;
            e.packed = (physTile & 0xFFFu)
                     | ((valid ? 1u : 0u) << 12)
                     | ((mipDelta & 0x7u) << 13)
                     | ((channelMask & 0x1Fu) << 16);
            return e;
        }

        bool isValid() const { return (packed >> 12) & 1u; }
        uint32_t physicalTile() const { return packed & 0xFFFu; }
        uint32_t mipDelta() const { return (packed >> 13) & 0x7u; }
        uint32_t channelMask() const { return (packed >> 16) & 0x1Fu; }
        bool hasChannel(uint32_t ch) const { return (channelMask() >> ch) & 1u; }

        static SVTPageTableEntry invalid() { return {}; }
    };
    static_assert(sizeof(SVTPageTableEntry) == 4);

    // ---- Virtual Tile Coordinate ----

    struct VirtualTileCoord
    {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t mipLevel = 0;

        bool operator==(const VirtualTileCoord& o) const
        {
            return x == o.x && y == o.y && mipLevel == o.mipLevel;
        }
    };

    struct VirtualTileCoordHash
    {
        size_t operator()(const VirtualTileCoord& c) const
        {
            size_t h = std::hash<uint32_t>{}(c.x);
            h ^= std::hash<uint32_t>{}(c.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<uint32_t>{}(c.mipLevel) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    // ---- Physical Tile Info (CPU tracking) ----

    struct PhysicalTileInfo
    {
        VirtualTileCoord virtualCoord{};
        uint64_t lastUsedFrame = 0;
        bool occupied = false;
    };

    // ---- SVT Parameters (GPU UBO) ----

    // Descriptor set 12 binding layout:
    //   binding 0: Page table SSBO (uint[], 2 uints per entry)
    //   binding 1: Feedback buffer SSBO (used by feedback compute, not fragment)
    //   binding 2: SVT params UBO (SVTParamsGPU)
    //   binding 3: svtAlbedoCache (sampler2DArray)
    //   binding 4: svtNormalCache (sampler2DArray)
    //   binding 5: svtORMCache (sampler2DArray)

    struct alignas(16) SVTParamsGPU
    {
        glm::vec4 svtScaleOffset;       // xy = scale, zw = offset  (worldPos.xz * scale + offset = virtualUV)
        glm::uvec4 svtInfo;             // x = virtualSizeLog2, y = tileSizeLog2, z = physicalTileCount, w = mipLevels
        glm::uvec4 svtCacheIndices0;    // x = albedoCacheBindlessIdx, y = normalCacheBindlessIdx, z = ormCacheBindlessIdx, w = emissionCacheBindlessIdx
        glm::uvec4 svtCacheIndices1;    // x = heightCacheBindlessIdx, y = unused, z = unused, w = unused
        glm::uvec4 svtTileInfo;         // x = borderSize, y = physicalTileSize, z = pageTableEntryCount, w = svtEnabled
    };
    static_assert(sizeof(SVTParamsGPU) == 80);

    // ---- Feedback request flags ----

    constexpr uint32_t SVT_FEEDBACK_REQUEST_FLAG = 0x80000000u;  // Bit 31: tile was requested
    // Bits [0..3]: requested mip level

    // ---- Mip level utilities ----

    inline uint32_t computeMipLevelCount(uint32_t virtualSizeLog2, uint32_t tileSizeLog2)
    {
        return virtualSizeLog2 - tileSizeLog2 + 1;
    }

    inline uint32_t computeTilesPerMipSide(uint32_t mipLevel, uint32_t virtualSizeLog2, uint32_t tileSizeLog2)
    {
        uint32_t mip0Tiles = 1u << (virtualSizeLog2 - tileSizeLog2);
        return mip0Tiles >> mipLevel;
    }

    // Compute total page table entries across all mip levels
    inline uint32_t computeTotalPageTableEntries(uint32_t virtualSizeLog2, uint32_t tileSizeLog2)
    {
        uint32_t total = 0;
        uint32_t mipCount = computeMipLevelCount(virtualSizeLog2, tileSizeLog2);
        for (uint32_t m = 0; m < mipCount; ++m)
        {
            uint32_t tilesPerSide = computeTilesPerMipSide(m, virtualSizeLog2, tileSizeLog2);
            if (tilesPerSide == 0) tilesPerSide = 1;
            total += tilesPerSide * tilesPerSide;
        }
        return total;
    }

    // Compute the page table offset for a given mip level
    inline uint32_t computePageTableMipOffset(uint32_t mipLevel, uint32_t virtualSizeLog2, uint32_t tileSizeLog2)
    {
        uint32_t offset = 0;
        for (uint32_t m = 0; m < mipLevel; ++m)
        {
            uint32_t tilesPerSide = computeTilesPerMipSide(m, virtualSizeLog2, tileSizeLog2);
            if (tilesPerSide == 0) tilesPerSide = 1;
            offset += tilesPerSide * tilesPerSide;
        }
        return offset;
    }
}
