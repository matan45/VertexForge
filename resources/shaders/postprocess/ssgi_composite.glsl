#type VERTEX
#version 460 core

layout(location = 0) out vec2 fragTexCoord;

void main() {
    fragTexCoord = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragTexCoord * 2.0 - 1.0, 0.0, 1.0);
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneColor;
layout(set = 0, binding = 1) uniform sampler2D denoisedSSGI;

void main() {
    vec3 scene = texture(sceneColor, fragTexCoord).rgb;
    vec3 indirect = texture(denoisedSSGI, fragTexCoord).rgb;

    // Additive composite of indirect lighting
    outColor = vec4(indirect, 1.0);
}
