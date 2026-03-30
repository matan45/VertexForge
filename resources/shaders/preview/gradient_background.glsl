#type VERTEX
#version 460 core

layout(location = 0) out vec2 texCoord;

void main() {
    // Fullscreen triangle: 3 vertices cover entire NDC [-1,1] range
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    texCoord = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    vec4 topColor;
    vec4 bottomColor;
} pc;

void main() {
    float t = texCoord.y;
    outColor = mix(pc.topColor, pc.bottomColor, t);
}
