// SVT virtual texture sampling functions
//
// Before including this file, declare:
//   - SVT_PAGE_TABLE_DATA: name of the uint[] SSBO holding page table entries (2 uints per entry)
//   - SVT_PARAMS: name of the SVTParams uniform
//   - svtAlbedoCache: sampler2DArray for albedo physical cache
//   - svtNormalCache: sampler2DArray for normal physical cache
//   - svtORMCache: sampler2DArray for ORM physical cache
//
// Example:
//   layout(std430, set = 12, binding = 0) readonly buffer SVTPageTableBuf { uint svtPageTableData[]; };
//   layout(set = 12, binding = 2) uniform SVTParamsUBO { SVTParams svtParams; };
//   layout(set = 12, binding = 3) uniform sampler2DArray svtAlbedoCache;
//   ...
//   #define SVT_PAGE_TABLE_DATA svtPageTableData
//   #define SVT_PARAMS svtParams
//   #include "svt_sampling.glsl"

#ifndef SVT_SAMPLING_GLSL
#define SVT_SAMPLING_GLSL

#include "svt_types.glsl"

struct SVTSampleResult {
    vec4 albedo;
    vec3 normal;
    vec3 orm;         // R = AO, G = roughness, B = metallic
    float mipLevel;   // Resolved mip level (for debug vis)
    bool isResident;  // True if exact or fallback mip was found
};

SVTSampleResult sampleSVT(vec2 worldXZ, vec2 dWorlddx, vec2 dWorlddy) {
    SVTSampleResult result;
    result.albedo = vec4(1.0, 0.0, 1.0, 1.0); // Magenta = missing
    result.normal = vec3(0.0, 0.0, 1.0);
    result.orm = vec3(1.0, 0.5, 0.0);
    result.mipLevel = 0.0;
    result.isResident = false;

    // Compute virtual UV
    vec2 virtualUV = worldXZ * SVT_PARAMS.svtScaleOffset.xy + SVT_PARAMS.svtScaleOffset.zw;
    if (any(lessThan(virtualUV, vec2(0.0))) || any(greaterThanEqual(virtualUV, vec2(1.0))))
        return result;

    uint vsLog2 = SVT_PARAMS.svtInfo.x;
    uint tsLog2 = SVT_PARAMS.svtInfo.y;
    uint mipLevels = SVT_PARAMS.svtInfo.w;

    // Compute mip level from screen-space derivatives
    vec2 dUVdx = dWorlddx * SVT_PARAMS.svtScaleOffset.xy;
    vec2 dUVdy = dWorlddy * SVT_PARAMS.svtScaleOffset.xy;
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

        // Read page table entry (2 uints)
        uint w0 = SVT_PAGE_TABLE_DATA[pageIdx * 2u];
        uint w1 = SVT_PAGE_TABLE_DATA[pageIdx * 2u + 1u];
        SVTPageEntry entry;
        entry.word0 = w0;
        entry.word1 = w1;

        if (svtEntryIsValid(entry)) {
            // Compute UV within the tile
            vec2 tileUV = fract(virtualUV * float(tps));

            // Map to physical tile texel space with border
            float border = float(SVT_BORDER_SIZE);
            float physSize = float(SVT_PHYSICAL_TILE_SIZE);
            vec2 physUV = (tileUV * float(SVT_TILE_SIZE) + border) / physSize;

            // Sample from physical caches
            uint albedoTile = svtEntryAlbedoTile(entry);
            uint normalTile = svtEntryNormalTile(entry);
            uint ormTile = svtEntryORMTile(entry);

            result.albedo = texture(svtAlbedoCache, vec3(physUV, float(albedoTile)));
            result.normal = texture(svtNormalCache, vec3(physUV, float(normalTile))).rgb * 2.0 - 1.0;
            result.orm = texture(svtORMCache, vec3(physUV, float(ormTile))).rgb;
            result.mipLevel = float(m);
            result.isResident = true;
            return result;
        }
    }

    // No resident tile found at any mip
    return result;
}

// Convenience: sample from world position using fragment derivatives
SVTSampleResult sampleSVTFromWorld(vec3 worldPos) {
    vec2 dWorlddx = dFdx(worldPos.xz);
    vec2 dWorlddy = dFdy(worldPos.xz);
    return sampleSVT(worldPos.xz, dWorlddx, dWorlddy);
}

#endif // SVT_SAMPLING_GLSL
