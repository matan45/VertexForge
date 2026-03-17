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
    encoded.shR0 = sign(traceResult.shR0) * pow(abs(traceResult.shR0), vec4(INV_GAMMA));
    encoded.shR1 = sign(traceResult.shR1) * pow(abs(traceResult.shR1), vec4(INV_GAMMA));
    encoded.shR2 = sign(traceResult.shR2) * pow(abs(traceResult.shR2), vec4(INV_GAMMA));
    encoded.shG0 = sign(traceResult.shG0) * pow(abs(traceResult.shG0), vec4(INV_GAMMA));
    encoded.shG1 = sign(traceResult.shG1) * pow(abs(traceResult.shG1), vec4(INV_GAMMA));
    encoded.shG2 = sign(traceResult.shG2) * pow(abs(traceResult.shG2), vec4(INV_GAMMA));
    encoded.shB0 = sign(traceResult.shB0) * pow(abs(traceResult.shB0), vec4(INV_GAMMA));
    encoded.shB1 = sign(traceResult.shB1) * pow(abs(traceResult.shB1), vec4(INV_GAMMA));
    encoded.shB2 = sign(traceResult.shB2) * pow(abs(traceResult.shB2), vec4(INV_GAMMA));
    encoded.validity = traceResult.validity;

    // Probe validity check: if too many backface hits, mark as invalid
    if (traceResult.validity.z > 0.25) {
        encoded.validity.x *= 0.5; // Reduce weight for probes inside geometry
    }

    probeDataWrite[globalProbeIndex] = encoded;
}
