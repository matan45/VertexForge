#ifndef GI_COMMON_GLSL
#define GI_COMMON_GLSL

// Spherical Harmonics (L0 + L1 + L2 = 9 coefficients per channel)
// ProbeData: 9 x vec4 (3 per R, G, B channel) + validity vec4

struct ProbeData {
    vec4 shR0;      // R: L0, L1y, L1z, L1x
    vec4 shR1;      // R: L2_-2, L2_-1, L2_0, L2_1
    vec4 shR2;      // R: L2_2, pad, pad, pad
    vec4 shG0;      // G: L0, L1y, L1z, L1x
    vec4 shG1;      // G: L2_-2, L2_-1, L2_0, L2_1
    vec4 shG2;      // G: L2_2, pad, pad, pad
    vec4 shB0;      // B: L0, L1y, L1z, L1x
    vec4 shB1;      // B: L2_-2, L2_-1, L2_0, L2_1
    vec4 shB2;      // B: L2_2, pad, pad, pad
    vec4 validity;  // x=weight, y=age, z=backfaceHitRatio, w=reserved
};

struct CascadeInfo {
    vec4 gridOriginSpacing;  // xyz = origin, w = spacing
    ivec4 gridDimsOffset;    // xyz = grid dimensions, w = probeOffset
};

// SH basis constants (bands 0, 1, and 2)
const float SH_C0  = 0.282095;   // 1 / (2*sqrt(PI))
const float SH_C1  = 0.488603;   // sqrt(3) / (2*sqrt(PI))
const float SH_C2  = 1.092548;   // sqrt(15 / (4*PI))  — Y2,-2, Y2,-1, Y2,1
const float SH_C20 = 0.315392;   // sqrt(5 / (16*PI))  — Y2,0
const float SH_C22 = 0.546274;   // sqrt(15 / (16*PI)) — Y2,2

// Compute L2 SH basis for a direction
// b0 = [Y00, Y1,-1, Y10, Y11]  (L0 + L1)
// b1 = [Y2,-2, Y2,-1, Y20, Y21] (L2 part 1)
// b2 = Y2,2                      (L2 part 2)
void shBasisL2(vec3 d, out vec4 b0, out vec4 b1, out float b2) {
    b0 = vec4(
        SH_C0,
        SH_C1 * d.y,
        SH_C1 * d.z,
        SH_C1 * d.x
    );
    b1 = vec4(
        SH_C2  * d.x * d.y,
        SH_C2  * d.y * d.z,
        SH_C20 * (3.0 * d.z * d.z - 1.0),
        SH_C2  * d.x * d.z
    );
    b2 = SH_C22 * (d.x * d.x - d.y * d.y);
}

vec3 evaluateSH(ProbeData probe, vec3 normal) {
    vec4 b0; vec4 b1; float b2;
    shBasisL2(normal, b0, b1, b2);
    return vec3(
        dot(probe.shR0, b0) + dot(probe.shR1, b1) + probe.shR2.x * b2,
        dot(probe.shG0, b0) + dot(probe.shG1, b1) + probe.shG2.x * b2,
        dot(probe.shB0, b0) + dot(probe.shB1, b1) + probe.shB2.x * b2
    );
}

void accumulateSH(inout vec4 shR0, inout vec4 shR1, inout vec4 shR2,
                  inout vec4 shG0, inout vec4 shG1, inout vec4 shG2,
                  inout vec4 shB0, inout vec4 shB1, inout vec4 shB2,
                  vec3 direction, vec3 radiance) {
    vec4 b0; vec4 b1; float b2;
    shBasisL2(direction, b0, b1, b2);

    vec4 b2v = vec4(b2, 0.0, 0.0, 0.0);

    shR0 += b0  * radiance.r;
    shR1 += b1  * radiance.r;
    shR2 += b2v * radiance.r;
    shG0 += b0  * radiance.g;
    shG1 += b1  * radiance.g;
    shG2 += b2v * radiance.g;
    shB0 += b0  * radiance.b;
    shB1 += b1  * radiance.b;
    shB2 += b2v * radiance.b;
}

ivec3 probeIndexToGrid(uint probeIndex, ivec3 gridDims) {
    int x = int(probeIndex) % gridDims.x;
    int y = (int(probeIndex) / gridDims.x) % gridDims.y;
    int z = int(probeIndex) / (gridDims.x * gridDims.y);
    return ivec3(x, y, z);
}

vec3 probeWorldPosition(uint probeIndex, CascadeInfo cascade) {
    ivec3 gridCoord = probeIndexToGrid(probeIndex, cascade.gridDimsOffset.xyz);
    return cascade.gridOriginSpacing.xyz + vec3(gridCoord) * cascade.gridOriginSpacing.w;
}

void findSurroundingProbes(vec3 worldPos, CascadeInfo cascade,
                           out ivec3 baseCoord, out vec3 alpha) {
    vec3 localPos = (worldPos - cascade.gridOriginSpacing.xyz) / cascade.gridOriginSpacing.w;
    baseCoord = ivec3(floor(localPos));
    alpha = fract(localPos);

    ivec3 maxCoord = cascade.gridDimsOffset.xyz - ivec3(2);
    baseCoord = clamp(baseCoord, ivec3(0), maxCoord);
}

uint gridToProbeIndex(ivec3 coord, ivec3 gridDims) {
    return uint(coord.x + coord.y * gridDims.x + coord.z * gridDims.x * gridDims.y);
}

#endif // GI_COMMON_GLSL
