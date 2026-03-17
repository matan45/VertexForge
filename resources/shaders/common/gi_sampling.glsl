#ifndef GI_SAMPLING_GLSL
#define GI_SAMPLING_GLSL

// GI Sampling for PBR fragment shaders
// Provides trilinear interpolated probe irradiance from radiance cascades
// Requires GI_ENABLED define and set 11 binding

#ifdef GI_ENABLED

struct GIProbeData {
    vec4 shR0;
    vec4 shR1;
    vec4 shR2;
    vec4 shG0;
    vec4 shG1;
    vec4 shG2;
    vec4 shB0;
    vec4 shB1;
    vec4 shB2;
    vec4 validity;
};

struct GICascadeInfo {
    vec4 gridOriginSpacing;
    ivec4 gridDimsOffset;
};

layout(set = 11, binding = 0) readonly buffer GIProbeBuffer {
    GIProbeData giProbes[];
};

layout(set = 11, binding = 1) readonly buffer GICascadeBuffer {
    uint giCascadeCount;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    GICascadeInfo giCascades[];
};

// SH basis constants (L0 + L1 + L2)
const float GI_SH_C0  = 0.282095;
const float GI_SH_C1  = 0.488603;
const float GI_SH_C2  = 1.092548;
const float GI_SH_C20 = 0.315392;
const float GI_SH_C22 = 0.546274;

vec3 evaluateGISH(GIProbeData probe, vec3 normal) {
    vec4 b0 = vec4(
        GI_SH_C0,
        GI_SH_C1 * normal.y,
        GI_SH_C1 * normal.z,
        GI_SH_C1 * normal.x
    );
    vec4 b1 = vec4(
        GI_SH_C2  * normal.x * normal.y,
        GI_SH_C2  * normal.y * normal.z,
        GI_SH_C20 * (3.0 * normal.z * normal.z - 1.0),
        GI_SH_C2  * normal.x * normal.z
    );
    float b2 = GI_SH_C22 * (normal.x * normal.x - normal.y * normal.y);

    return max(vec3(
        dot(probe.shR0, b0) + dot(probe.shR1, b1) + probe.shR2.x * b2,
        dot(probe.shG0, b0) + dot(probe.shG1, b1) + probe.shG2.x * b2,
        dot(probe.shB0, b0) + dot(probe.shB1, b1) + probe.shB2.x * b2
    ), vec3(0.0));
}

// Internal: sample irradiance from a specific cascade via trilinear interpolation
// Returns interpolated irradiance and sets outCoverage (0-1) based on how well
// the fragment is covered by the cascade grid (0 = at edge/outside, 1 = well inside)
vec3 sampleCascadeIrradiance(uint cascadeIdx, vec3 worldPos, vec3 normal, out float outCoverage) {
    GICascadeInfo cascade = giCascades[cascadeIdx];
    float spacing = cascade.gridOriginSpacing.w;
    vec3 origin = cascade.gridOriginSpacing.xyz;
    ivec3 gridDims = cascade.gridDimsOffset.xyz;
    int probeOffset = cascade.gridDimsOffset.w;

    // Find surrounding probes
    vec3 localPos = (worldPos - origin) / spacing;
    ivec3 baseCoord = ivec3(floor(localPos));
    vec3 alpha = fract(localPos);

    // Compute coverage: how far inside the grid the fragment is (0=edge, 1=center)
    vec3 normalizedPos = localPos / vec3(gridDims);
    vec3 edgeDist = min(normalizedPos, vec3(1.0) - normalizedPos) * 2.0;
    outCoverage = clamp(min(edgeDist.x, min(edgeDist.y, edgeDist.z)) * 4.0, 0.0, 1.0);

    baseCoord = clamp(baseCoord, ivec3(0), gridDims - ivec3(2));

    // Trilinear interpolation of 8 surrounding probes
    vec3 irradiance = vec3(0.0);
    float totalWeight = 0.0;

    for (int dz = 0; dz <= 1; ++dz) {
        for (int dy = 0; dy <= 1; ++dy) {
            for (int dx = 0; dx <= 1; ++dx) {
                ivec3 coord = baseCoord + ivec3(dx, dy, dz);

                // Linear index
                uint probeIdx = uint(probeOffset) +
                    uint(coord.x + coord.y * gridDims.x + coord.z * gridDims.x * gridDims.y);

                GIProbeData probe = giProbes[probeIdx];

                // Trilinear weight
                vec3 w = mix(vec3(1.0) - alpha, alpha, vec3(dx, dy, dz));
                float triWeight = w.x * w.y * w.z;

                // Weight by probe validity
                float validityWeight = probe.validity.x;
                float weight = triWeight * validityWeight;

                if (weight > 0.0) {
                    irradiance += evaluateGISH(probe, normal) * weight;
                    totalWeight += weight;
                }
            }
        }
    }

    if (totalWeight > 0.0) {
        irradiance /= totalWeight;
    }

    return irradiance;
}

// Trilinear probe interpolation with far-field cascade support
vec3 sampleProbeGI(vec3 worldPos, vec3 normal, float cameraDistance) {
    if (giCascadeCount == 0) return vec3(0.0);

    // Select cascade based on camera distance
    uint selectedCascade = 0;
    for (uint i = 1; i < giCascadeCount; ++i) {
        float cascadeRange = giCascades[i].gridOriginSpacing.w *
                             float(giCascades[i].gridDimsOffset.x) * 0.5;
        if (cameraDistance > cascadeRange * 0.7) {
            selectedCascade = i;
        }
    }

    float coverage = 1.0;
    vec3 irradiance = sampleCascadeIrradiance(selectedCascade, worldPos, normal, coverage);

    // At the outermost cascade edges, smoothly fade to zero (IBL will fill the gap)
    // This prevents hard cutoff at the GI boundary
    if (selectedCascade == giCascadeCount - 1 && coverage < 1.0) {
        irradiance *= coverage;
    }

    // Clamp to reasonable range to prevent flickering from unstable probes
    return clamp(irradiance, vec3(0.0), vec3(5.0));
}

#endif // GI_ENABLED

#endif // GI_SAMPLING_GLSL
