#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
    mat4 viewProj;           
    mat4 inverseViewProj;    
    vec4 color;
} pc;

void main() {
    vec4 worldPos = pc.inverseViewProj * vec4(inPosition, 1.0);
    worldPos /= worldPos.w; 

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
