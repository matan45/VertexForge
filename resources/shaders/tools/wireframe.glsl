#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;           // Editor's view-projection matrix
    mat4 inverseViewProj;    // Camera's inverse view-projection (for frustum rendering)
    vec4 color;
} pc;

void main() {
    // For frustum rendering: transform NDC corner to world space, then to clip space
    // inPosition contains NDC coordinates (x, y, z in [-1,1] or [0,1] range)
    vec4 worldPos = pc.inverseViewProj * vec4(inPosition, 1.0);
    worldPos /= worldPos.w;  // Perspective divide to get world position

    gl_Position = pc.viewProj * vec4(worldPos.xyz, 1.0);
}

#type FRAGMENT
#version 460 core

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    mat4 inverseViewProj;
    vec4 color;
} pc;

void main() {
    outColor = pc.color;
}
