#type COMPUTE
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "gi_common.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Set 0: Probe data (read from previous + write to current)
layout(std430, set = 0, binding = 0) readonly buffer ProbeReadBuffer {
    ProbeData probeDataRead[];
};

layout(std430, set = 0, binding = 1) writeonly buffer ProbeWriteBuffer {
    ProbeData probeDataWrite[];
};

// Set 1: Cascade info (buffer has 16-byte header: cascadeCount + padding)
layout(std140, set = 1, binding = 0) uniform CascadeInfoUBO {
    uint cascadeCount;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    CascadeInfo cascades[8];
};

layout(push_constant) uniform PushConstants {
    uint cascadeIndex;
    uint probeStartIndex;
    uint probeCount;
    uint raysPerProbe;
    float maxDistance;
    float temporalBlend;
    float frameRandom;
    uint frameIndex;
};

void main() {
    uint localIndex = gl_GlobalInvocationID.x;
    if (localIndex >= probeCount) return;

    uint globalProbeIndex = probeStartIndex + localIndex;

    ProbeData traceResult = probeDataRead[globalProbeIndex];

    // Apply irradiance encoding (gamma compression for better precision)
    const float GAMMA = 5.0;
    const float INV_GAMMA = 1.0 / GAMMA;

    ProbeData encoded;
    encoded.shR = sign(traceResult.shR) * pow(abs(traceResult.shR), vec4(INV_GAMMA));
    encoded.shG = sign(traceResult.shG) * pow(abs(traceResult.shG), vec4(INV_GAMMA));
    encoded.shB = sign(traceResult.shB) * pow(abs(traceResult.shB), vec4(INV_GAMMA));
    encoded.validity = traceResult.validity;

    // Probe validity check: if too many backface hits, mark as invalid
    if (traceResult.validity.z > 0.25) {
        encoded.validity.x *= 0.5; // Reduce weight for probes inside geometry
    }

    probeDataWrite[globalProbeIndex] = encoded;
}
