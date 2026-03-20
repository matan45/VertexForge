// SVT (Sparse Virtual Texturing) shared GPU types
// Included by svt_feedback.glsl and svt_sampling.glsl

#ifndef SVT_TYPES_GLSL
#define SVT_TYPES_GLSL

// Page table entry layout (1x uint32 = 4 bytes)
// All 5 channels (albedo, normal, ORM, emission, height) share the same physical tile index.
//
//   bits [0..11]   physical tile index (same for all channels)
//   bit  [12]      valid flag
//   bits [13..15]  mip delta (how many mips coarser than requested)
//   bits [16..20]  channel presence mask (albedo=0, normal=1, ORM=2, emission=3, height=4)
//   bits [21..31]  reserved

bool svtEntryIsValid(uint entry) {
    return (entry & 0x1000u) != 0u;
}

uint svtEntryPhysTile(uint entry) {
    return entry & 0xFFFu;
}

uint svtEntryMipDelta(uint entry) {
    return (entry >> 13) & 0x7u;
}

uint svtEntryChannelMask(uint entry) {
    return (entry >> 16) & 0x1Fu;
}

bool svtEntryHasChannel(uint entry, uint channel) {
    return ((entry >> (16u + channel)) & 1u) != 0u;
}

// Channel indices
#define SVT_CH_ALBEDO   0u
#define SVT_CH_NORMAL   1u
#define SVT_CH_ORM      2u
#define SVT_CH_EMISSION 3u
#define SVT_CH_HEIGHT   4u

// SVT parameters UBO
struct SVTParams {
    vec4  svtScaleOffset;       // xy = scale, zw = offset (worldXZ * scale + offset = virtualUV)
    uvec4 svtInfo;              // x = virtualSizeLog2, y = tileSizeLog2, z = physicalTileCount, w = mipLevels
    uvec4 svtCacheIndices0;     // x = albedoCacheIdx, y = normalCacheIdx, z = ormCacheIdx, w = emissionCacheIdx
    uvec4 svtCacheIndices1;     // x = heightCacheIdx, y/z/w = unused
    uvec4 svtTileInfo;          // x = borderSize, y = physicalTileSize, z = totalPageTableEntries, w = svtEnabled
};

// Constants
#define SVT_TILE_SIZE          128u
#define SVT_BORDER_SIZE        4u
#define SVT_PHYSICAL_TILE_SIZE 136u
#define SVT_FEEDBACK_REQUEST   0x80000000u

// Compute the number of tiles per side at a given mip level
uint svtTilesPerSide(uint mipLevel, uint virtualSizeLog2, uint tileSizeLog2) {
    uint mip0Tiles = 1u << (virtualSizeLog2 - tileSizeLog2);
    return max(mip0Tiles >> mipLevel, 1u);
}

// Compute page table offset for a mip level
uint svtPageTableMipOffset(uint mipLevel, uint virtualSizeLog2, uint tileSizeLog2) {
    uint offset = 0u;
    for (uint m = 0u; m < mipLevel; ++m) {
        uint tps = svtTilesPerSide(m, virtualSizeLog2, tileSizeLog2);
        offset += tps * tps;
    }
    return offset;
}

// Compute flat page table index for a virtual tile coordinate
uint svtPageTableIndex(uvec2 tileCoord, uint mipLevel, uint virtualSizeLog2, uint tileSizeLog2) {
    uint mipOffset = svtPageTableMipOffset(mipLevel, virtualSizeLog2, tileSizeLog2);
    uint tps = svtTilesPerSide(mipLevel, virtualSizeLog2, tileSizeLog2);
    return mipOffset + tileCoord.y * tps + tileCoord.x;
}

#endif // SVT_TYPES_GLSL
