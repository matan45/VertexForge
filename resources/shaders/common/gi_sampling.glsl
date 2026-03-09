#ifndef GI_SAMPLING_GLSL
#define GI_SAMPLING_GLSL

// GI Sampling for PBR fragment shaders
// Provides trilinear interpolated probe irradiance from radiance cascades
// Requires GI_ENABLED define and set 11 binding

#ifdef GI_ENABLED

struct GIProbeData {
    vec4 shR;
    vec4 shG;
    vec4 shB;
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

// SH basis evaluation
const float GI_SH_C0 = 0.282095;
const float GI_SH_C1 = 0.488603;

vec3 evaluateGISH(GIProbeData probe, vec3 normal) {
    vec4 basis = vec4(GI_SH_C0, GI_SH_C1 * normal.y, GI_SH_C1 * normal.z, GI_SH_C1 * normal.x);
    return max(vec3(
        dot(probe.shR, basis),
        dot(probe.shG, basis),
        dot(probe.shB, basis)
    ), vec3(0.0));
}

// Trilinear probe interpolation
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

    GICascadeInfo cascade = giCascades[selectedCascade];
    float spacing = cascade.gridOriginSpacing.w;
    vec3 origin = cascade.gridOriginSpacing.xyz;
    ivec3 gridDims = cascade.gridDimsOffset.xyz;
    int probeOffset = cascade.gridDimsOffset.w;

    // Find surrounding probes
    vec3 localPos = (worldPos - origin) / spacing;
    ivec3 baseCoord = ivec3(floor(localPos));
    vec3 alpha = fract(localPos);

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

    // Decode gamma encoding
    const float GAMMA = 5.0;
    irradiance = sign(irradiance) * pow(abs(irradiance), vec3(GAMMA));

    return max(irradiance, vec3(0.0));
}

#endif // GI_ENABLED

#endif // GI_SAMPLING_GLSL
