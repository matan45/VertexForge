#ifndef GI_COMMON_GLSL
#define GI_COMMON_GLSL

// Spherical Harmonics (L0 + L1 = 4 coefficients per channel)
// ProbeData: 3 x vec4 (R, G, B SH coefficients) + validity vec4

struct ProbeData {
    vec4 shR;       // L0, L1x, L1y, L1z for Red
    vec4 shG;       // L0, L1x, L1y, L1z for Green
    vec4 shB;       // L0, L1x, L1y, L1z for Blue
    vec4 validity;  // x=weight, y=age, z=backfaceHitRatio, w=reserved
};

struct CascadeInfo {
    vec4 gridOriginSpacing;  // xyz = origin, w = spacing
    ivec4 gridDimsOffset;    // xyz = grid dimensions, w = probeOffset
};

// SH basis functions (band 0 and band 1)
const float SH_C0 = 0.282095;   // 1 / (2*sqrt(PI))
const float SH_C1 = 0.488603;   // sqrt(3) / (2*sqrt(PI))

vec4 shBasis(vec3 dir) {
    return vec4(
        SH_C0,
        SH_C1 * dir.y,
        SH_C1 * dir.z,
        SH_C1 * dir.x
    );
}

// Evaluate SH irradiance for a given direction
vec3 evaluateSH(ProbeData probe, vec3 normal) {
    vec4 basis = shBasis(normal);
    return vec3(
        dot(probe.shR, basis),
        dot(probe.shG, basis),
        dot(probe.shB, basis)
    );
}

// Accumulate radiance into SH for a given direction
void accumulateSH(inout vec4 shR, inout vec4 shG, inout vec4 shB,
                  vec3 direction, vec3 radiance) {
    vec4 basis = shBasis(direction);
    shR += basis * radiance.r;
    shG += basis * radiance.g;
    shB += basis * radiance.b;
}

// Get 3D grid index from linear probe index
ivec3 probeIndexToGrid(uint probeIndex, ivec3 gridDims) {
    int x = int(probeIndex) % gridDims.x;
    int y = (int(probeIndex) / gridDims.x) % gridDims.y;
    int z = int(probeIndex) / (gridDims.x * gridDims.y);
    return ivec3(x, y, z);
}

// Get world position of a probe
vec3 probeWorldPosition(uint probeIndex, CascadeInfo cascade) {
    ivec3 gridCoord = probeIndexToGrid(probeIndex, cascade.gridDimsOffset.xyz);
    return cascade.gridOriginSpacing.xyz + vec3(gridCoord) * cascade.gridOriginSpacing.w;
}

// Find 8 nearest probes for trilinear interpolation
// Returns base grid coord and fractional offset
void findSurroundingProbes(vec3 worldPos, CascadeInfo cascade,
                           out ivec3 baseCoord, out vec3 alpha) {
    vec3 localPos = (worldPos - cascade.gridOriginSpacing.xyz) / cascade.gridOriginSpacing.w;
    baseCoord = ivec3(floor(localPos));
    alpha = fract(localPos);

    // Clamp to valid range
    ivec3 maxCoord = cascade.gridDimsOffset.xyz - ivec3(2);
    baseCoord = clamp(baseCoord, ivec3(0), maxCoord);
}

// Convert grid coord to linear probe index
uint gridToProbeIndex(ivec3 coord, ivec3 gridDims) {
    return uint(coord.x + coord.y * gridDims.x + coord.z * gridDims.x * gridDims.y);
}

#endif // GI_COMMON_GLSL
