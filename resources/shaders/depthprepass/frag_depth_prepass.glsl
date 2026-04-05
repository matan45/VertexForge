#type FRAGMENT
#version 460 core

layout(location = 0) in vec3 inWorldNormal;
layout(location = 0) out vec4 outNormal;

void main() {
    // RGB = world-space normal, A = roughness (default 0.5, refined when material data available)
    outNormal = vec4(normalize(inWorldNormal), 0.5);
}
