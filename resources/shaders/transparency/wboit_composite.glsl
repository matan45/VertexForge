#type VERTEX
#version 460 core

layout(location = 0) out vec2 fragUV;

void main() {
    // Fullscreen triangle
    fragUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(fragUV * 2.0 - 1.0, 0.0, 1.0);
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D accumTexture;
layout(set = 0, binding = 1) uniform sampler2D revealageTexture;

void main() {
    vec4 accum = texture(accumTexture, fragUV);
    float revealage = texture(revealageTexture, fragUV).r;

    // If revealage is 1.0, nothing transparent was drawn here
    if (revealage >= 1.0) {
        discard;
    }

    // Reconstruct average color: accum.rgb / max(accum.a, 1e-5)
    vec3 averageColor = accum.rgb / max(accum.a, 1e-5);

    // Output with alpha = (1 - revealage) for blending onto opaque
    outColor = vec4(averageColor, 1.0 - revealage);
}
