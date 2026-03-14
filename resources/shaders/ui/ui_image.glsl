#type VERTEX
#version 460 core

// Quad vertices (binding 0, per-vertex)
layout(location = 0) in vec2 inPosition;   // Quad corner offset (-0.5 to 0.5)
layout(location = 1) in vec2 inTexCoord;   // UV coordinates (0.0 to 1.0)

// Per-instance data (binding 1)
layout(location = 2) in vec4 inPosAndSize;  // xy = pixel position, zw = pixel size
layout(location = 3) in vec4 inColorTint;   // RGBA color tint
layout(location = 4) in vec4 inUVRect;      // u0, v0, u1, v1

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColorTint;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    float alphaThreshold;
    uint flags;
} pc;

void main() {
    // Map quad position (-0.5..0.5) to (0..1)
    vec2 localPos = inPosition + 0.5;

    // Compute pixel position of this vertex
    vec2 pixelPos = inPosAndSize.xy + localPos * inPosAndSize.zw;

    // Convert to NDC: Vulkan Y goes top(-1) to bottom(+1), matching pixel coords
    vec2 ndc = (pixelPos / pc.viewportSize) * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);
    fragTexCoord = mix(inUVRect.xy, inUVRect.zw, inTexCoord);
    fragColorTint = inColorTint;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColorTint;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D uiTexture;

layout(push_constant) uniform PushConstants {
    vec2 viewportSize;
    float alphaThreshold;
    uint flags;
} pc;

void main() {
    vec4 texColor = texture(uiTexture, fragTexCoord);
    outColor = texColor * fragColorTint;

    // Stencil write mode: discard transparent mask pixels so stencil isn't written there
    if (pc.flags == 1u && outColor.a < pc.alphaThreshold) {
        discard;
    }

    if (outColor.a < 0.01) {
        discard;
    }
}
