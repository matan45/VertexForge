// SVT (Sparse Virtual Texturing) shared GPU types
// Included by svt_feedback.glsl and svt_sampling.glsl

#ifndef SVT_TYPES_GLSL
#define SVT_TYPES_GLSL

// Page table entry layout (2x uint32 = 8 bytes)
//
// word0:
//   bits [0..11]   albedo physical tile index
//   bit  [12]      valid flag
//   bits [13..15]  mip delta (how many mips coarser than requested)
//   bits [16..31]  reserved
//
// word1:
//   bits [0..11]   normal physical tile index
//   bits [12..23]  ORM physical tile index
//   bits [24..31]  flags / reserved

struct SVTPageEntry {
    uint word0;
    uint word1;
};

bool svtEntryIsValid(SVTPageEntry e) {
    return (e.word0 & 0x1000u) != 0u;
}

uint svtEntryAlbedoTile(SVTPageEntry e) {
    return e.word0 & 0xFFFu;
}

uint svtEntryNormalTile(SVTPageEntry e) {
    return e.word1 & 0xFFFu;
}

uint svtEntryORMTile(SVTPageEntry e) {
    return (e.word1 >> 12) & 0xFFFu;
}

uint svtEntryMipDelta(SVTPageEntry e) {
    return (e.word0 >> 13) & 0x7u;
}

// SVT parameters UBO
struct SVTParams {
    vec4  svtScaleOffset;      // xy = scale, zw = offset (worldXZ * scale + offset = virtualUV)
    uvec4 svtInfo;             // x = virtualSizeLog2, y = tileSizeLog2, z = physicalTileCount, w = mipLevels
    uvec4 svtCacheIndices;     // x = albedoCacheIdx, y = normalCacheIdx, z = ormCacheIdx
    uvec4 svtTileInfo;         // x = borderSize, y = physicalTileSize, z = totalPageTableEntries, w = svtEnabled
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
