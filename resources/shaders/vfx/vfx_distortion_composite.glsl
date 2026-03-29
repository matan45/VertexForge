#type VERTEX
#version 460 core

layout(location = 0) out vec2 texCoord;

void main()
{
    // Fullscreen triangle: 3 vertices cover entire NDC [-1,1] range
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    texCoord = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColorCopy;
layout(set = 0, binding = 1) uniform sampler2D distortionBuffer;

void main()
{
    vec2 distortion = texture(distortionBuffer, texCoord).rg;
    vec2 distortedUV = clamp(texCoord + distortion, vec2(0.001), vec2(0.999));
    outColor = texture(sceneColorCopy, distortedUV);
}
