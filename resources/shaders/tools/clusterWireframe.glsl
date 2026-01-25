#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;  // Unit cube vertex [-1, 1]

struct ClusterInstance {
    vec4 minPoint;  // xyz = view-space min
    vec4 maxPoint;  // xyz = view-space max
    vec4 color;
};

layout(std430, set = 0, binding = 0) readonly buffer ClusterInstanceBuffer {
    ClusterInstance instances[];
};

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    mat4 invViewMatrix;
} pc;

layout(location = 0) out vec4 fragColor;

void main() {
    ClusterInstance inst = instances[gl_InstanceIndex];

    vec3 minView = inst.minPoint.xyz;
    vec3 maxView = inst.maxPoint.xyz;

    vec3 center = (minView + maxView) * 0.5;
    vec3 extents = (maxView - minView) * 0.5;

    vec3 viewPos = center + inPosition * extents;

    vec4 worldPos = pc.invViewMatrix * vec4(viewPos, 1.0);

    gl_Position = pc.viewProjection * worldPos;

    fragColor = inst.color;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = fragColor;
}
