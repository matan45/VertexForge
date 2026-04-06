#type FRAGMENT
#version 460 core

layout(location = 0) in vec3 inWorldNormal;
layout(location = 1) in float inRoughness;
layout(location = 0) out vec4 outNormal;

void main() {
    outNormal = vec4(normalize(inWorldNormal), inRoughness);
}
