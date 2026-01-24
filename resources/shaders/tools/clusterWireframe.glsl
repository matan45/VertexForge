#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;  // Unit cube vertex [-1, 1]

// Per-instance data from storage buffer
struct ClusterInstance {
    vec4 minPoint;   // xyz = view-space min, w = unused
    vec4 maxPoint;   // xyz = view-space max, w = highlighted flag
    vec4 color;      // RGBA color
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

    // Get view-space AABB bounds
    vec3 minView = inst.minPoint.xyz;
    vec3 maxView = inst.maxPoint.xyz;

    // Compute center and extents in view-space
    vec3 center = (minView + maxView) * 0.5;
    vec3 extents = (maxView - minView) * 0.5;

    // Transform unit cube vertex to view-space AABB position
    vec3 viewPos = center + inPosition * extents;

    // Transform to world-space
    vec4 worldPos = pc.invViewMatrix * vec4(viewPos, 1.0);

    // Project to clip space
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
