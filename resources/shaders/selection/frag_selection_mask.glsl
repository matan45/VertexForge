#type FRAGMENT
#version 460 core

// VK-1490 editor selection mask: any fragment that survives the LessOrEqual
// depth test against the resolved scene depth marks a visible selected pixel.
layout(location = 0) out vec4 outMask;

void main() {
    outMask = vec4(1.0);
}
