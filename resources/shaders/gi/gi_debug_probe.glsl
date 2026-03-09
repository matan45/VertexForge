#type VERTEX
#version 460 core
#extension GL_GOOGLE_include_directive : require

#include "gi_common.glsl"

layout(location = 0) out vec3 fragColor;
layout(location = 1) out float fragAlpha;

layout(std430, set = 0, binding = 0) readonly buffer ProbeDataBuffer {
    ProbeData probeData[];
};

layout(std140, set = 1, binding = 0) uniform CascadeInfoUBO {
    CascadeInfo cascades[8];
};

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    uint totalProbes;
    uint showMode;     // 0 = irradiance color, 1 = validity
    float probeSize;
    float padding;
};

void main() {
    uint probeIndex = gl_VertexIndex;
    if (probeIndex >= totalProbes) {
        gl_Position = vec4(0.0);
        gl_PointSize = 0.0;
        return;
    }

    // Find which cascade this probe belongs to
    uint cascadeIdx = 0;
    for (uint i = 0; i < 8; ++i) {
        if (cascades[i].gridDimsOffset.x == 0) break;
        uint cascadeEnd = uint(cascades[i].gridDimsOffset.w) +
            uint(cascades[i].gridDimsOffset.x * cascades[i].gridDimsOffset.y * cascades[i].gridDimsOffset.z);
        if (probeIndex < cascadeEnd) {
            cascadeIdx = i;
            break;
        }
    }

    uint localIndex = probeIndex - uint(cascades[cascadeIdx].gridDimsOffset.w);
    vec3 worldPos = probeWorldPosition(localIndex, cascades[cascadeIdx]);

    gl_Position = viewProjection * vec4(worldPos, 1.0);
    gl_PointSize = max(probeSize * 100.0 / max(gl_Position.w, 1.0), 2.0);

    ProbeData probe = probeData[probeIndex];

    if (showMode == 0) {
        // Show irradiance color (evaluate SH in up direction)
        fragColor = max(evaluateSH(probe, vec3(0, 1, 0)), vec3(0.0));
        fragAlpha = 0.8;
    } else {
        // Show validity: green = valid, red = invalid, blue = inside geometry
        float validity = probe.validity.x;
        float backface = probe.validity.z;
        fragColor = mix(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), validity);
        if (backface > 0.25) {
            fragColor = vec3(0.0, 0.0, 1.0);
        }
        fragAlpha = 0.6;
    }
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec3 fragColor;
layout(location = 1) in float fragAlpha;

layout(location = 0) out vec4 outColor;

void main() {
    // Render probe as a colored point/disc
    vec2 center = gl_PointCoord - vec2(0.5);
    float dist = length(center);
    if (dist > 0.5) discard;

    float alpha = smoothstep(0.5, 0.3, dist) * fragAlpha;
    outColor = vec4(fragColor, alpha);
}
