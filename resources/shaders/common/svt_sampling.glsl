// SVT virtual texture sampling functions
//
// Before including this file, declare:
//   - SVT_PAGE_TABLE_DATA: name of the uint[] SSBO holding page table entries (1 uint per entry)
//   - SVT_PARAMS: name of the SVTParams uniform
//   - svtAlbedoCache, svtNormalCache, svtORMCache: sampler2DArray for caches
//   - svtEmissionCache, svtHeightCache: sampler2DArray for optional caches

#ifndef SVT_SAMPLING_GLSL
#define SVT_SAMPLING_GLSL

#include "svt_types.glsl"

struct SVTSampleResult {
    vec4 albedo;
    vec3 normal;
    vec3 orm;          // R = AO, G = roughness, B = metallic
    vec3 emission;
    float height;
    float mipLevel;    // Resolved mip level (for debug vis)
    bool isResident;   // True if exact or fallback mip was found
    uint channelMask;  // Which channels are present
};

SVTSampleResult sampleSVT(vec2 virtualUVorTexCoord, vec2 dUVdx, vec2 dUVdy) {
    SVTSampleResult result;
    result.albedo = vec4(1.0, 0.0, 1.0, 1.0); // Magenta = missing
    result.normal = vec3(0.0, 0.0, 1.0);
    result.orm = vec3(1.0, 0.5, 0.0);
    result.emission = vec3(0.0);
    result.height = 0.0;
    result.mipLevel = 0.0;
    result.isResident = false;
    result.channelMask = 0u;

    // For terrain: virtualUV comes from worldXZ * scale + offset
    // For scene materials: virtualUV = texCoord directly (UV space is the virtual texture)
    vec2 virtualUV = virtualUVorTexCoord;

    // Clamp to valid range
    if (any(lessThan(virtualUV, vec2(0.0))) || any(greaterThanEqual(virtualUV, vec2(1.0))))
        return result;

    uint vsLog2 = SVT_PARAMS.svtInfo.x;
    uint tsLog2 = SVT_PARAMS.svtInfo.y;
    uint mipLevels = SVT_PARAMS.svtInfo.w;

    // Compute mip level from screen-space derivatives
    float virtualSize = float(1u << vsLog2);
    vec2 texGradX = dUVdx * virtualSize;
    vec2 texGradY = dUVdy * virtualSize;
    float maxGrad = max(dot(texGradX, texGradX), dot(texGradY, texGradY));
    float desiredMip = max(0.0, 0.5 * log2(maxGrad));
    uint mip = clamp(uint(desiredMip), 0u, mipLevels - 1u);

    // Walk mip chain to find a resident tile
    for (uint m = mip; m < mipLevels; ++m) {
        uint tps = svtTilesPerSide(m, vsLog2, tsLog2);
        uvec2 tileCoord = clamp(uvec2(virtualUV * float(tps)), uvec2(0), uvec2(tps - 1u));
        uint pageIdx = svtPageTableIndex(tileCoord, m, vsLog2, tsLog2);

        // Read page table entry (1 uint per entry)
        uint entry = SVT_PAGE_TABLE_DATA[pageIdx];

        if (svtEntryIsValid(entry)) {
            uint physTile = svtEntryPhysTile(entry);
            result.channelMask = svtEntryChannelMask(entry);

            // Compute UV within the tile
            vec2 tileUV = fract(virtualUV * float(tps));

            // Map to physical tile texel space with border
            float border = float(SVT_BORDER_SIZE);
            float physSize = float(SVT_PHYSICAL_TILE_SIZE);
            vec2 physUV = (tileUV * float(SVT_TILE_SIZE) + border) / physSize;

            // Sample from physical caches (all use same physTile index)
            if (svtEntryHasChannel(entry, SVT_CH_ALBEDO))
                result.albedo = texture(svtAlbedoCache, vec3(physUV, float(physTile)));

            if (svtEntryHasChannel(entry, SVT_CH_NORMAL))
                result.normal = texture(svtNormalCache, vec3(physUV, float(physTile))).rgb * 2.0 - 1.0;

            if (svtEntryHasChannel(entry, SVT_CH_ORM))
                result.orm = texture(svtORMCache, vec3(physUV, float(physTile))).rgb;

            if (svtEntryHasChannel(entry, SVT_CH_EMISSION))
                result.emission = texture(svtEmissionCache, vec3(physUV, float(physTile))).rgb;

            if (svtEntryHasChannel(entry, SVT_CH_HEIGHT))
                result.height = texture(svtHeightCache, vec3(physUV, float(physTile))).r;

            result.mipLevel = float(m);
            result.isResident = true;
            return result;
        }
    }

    // No resident tile found at any mip
    return result;
}

// Convenience: sample from world position using fragment derivatives (for terrain)
SVTSampleResult sampleSVTFromWorld(vec3 worldPos) {
    vec2 worldUV = worldPos.xz * SVT_PARAMS.svtScaleOffset.xy + SVT_PARAMS.svtScaleOffset.zw;
    vec2 dWorlddx = dFdx(worldPos.xz) * SVT_PARAMS.svtScaleOffset.xy;
    vec2 dWorlddy = dFdy(worldPos.xz) * SVT_PARAMS.svtScaleOffset.xy;
    return sampleSVT(worldUV, dWorlddx, dWorlddy);
}

#endif // SVT_SAMPLING_GLSL
