#type VERTEX
#version 460 core

// Quad vertex (binding 0)
layout(location = 0) in vec2 inPosition;   // -0.5..0.5
layout(location = 1) in vec2 inTexCoord;   // 0..1

// Ribbon segment instance data (binding 1)
layout(location = 2) in vec4 inPosASizeA;  // posA.xyz, sizeA
layout(location = 3) in vec4 inPosBSizeB;  // posB.xyz, sizeB
layout(location = 4) in vec4 inColorA;
layout(location = 5) in vec4 inColorB;
layout(location = 6) in float inTrailT;
layout(location = 7) in float inGlowIntensityA;
layout(location = 8) in float inGlowIntensityB;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragGlowIntensity;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
} camera;

layout(push_constant) uniform PushConstants {
    float alphaClipThreshold;
    uint blendMode;
    float ribbonWidth;
    float glowColorR;
    float glowColorG;
    float glowColorB;
    float emissiveIntensity;
    float uvScrollSpeedU;
    float uvScrollSpeedV;
} pc;

void main() {
    vec3 posA = inPosASizeA.xyz;
    float sizeA = inPosASizeA.w;
    vec3 posB = inPosBSizeB.xyz;
    float sizeB = inPosBSizeB.w;

    // along = 0..1 interpolates from posA to posB
    float along = inPosition.y + 0.5;
    vec3 pos = mix(posA, posB, along);
    float width = mix(sizeA, sizeB, along) * pc.ribbonWidth;

    // Camera-facing ribbon: compute right vector perpendicular to segment and camera direction
    vec3 segDir = posB - posA;
    float segLen = length(segDir);
    if (segLen < 0.0001) {
        segDir = vec3(0.0, 1.0, 0.0);
    } else {
        segDir = segDir / segLen;
    }

    vec3 toCamera = normalize(camera.cameraPos - pos);
    vec3 right = normalize(cross(toCamera, segDir));

    // Offset left/right by inPosition.x
    pos += right * inPosition.x * width;

    gl_Position = camera.projection * camera.view * vec4(pos, 1.0);

    // UV: x = trail position, y = across width
    fragTexCoord = vec2(inTrailT, inTexCoord.x);

    fragTexCoord += vec2(pc.uvScrollSpeedU, pc.uvScrollSpeedV) * camera.time;

    // Interpolate color and glow between endpoints
    fragColor = mix(inColorA, inColorB, along);
    fragGlowIntensity = mix(inGlowIntensityA, inGlowIntensityB, along);
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragGlowIntensity;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D particleTexture;

layout(push_constant) uniform PushConstants {
    float alphaClipThreshold;
    uint blendMode;
    float ribbonWidth;
    float glowColorR;
    float glowColorG;
    float glowColorB;
    float emissiveIntensity;
    float uvScrollSpeedU;
    float uvScrollSpeedV;
} pc;

void main() {
    vec4 texColor = texture(particleTexture, fragTexCoord);
    vec4 finalColor = texColor * fragColor;

    // Glow: additive emissive color
    vec3 glowColor = vec3(pc.glowColorR, pc.glowColorG, pc.glowColorB);
    finalColor.rgb += glowColor * fragGlowIntensity;
    finalColor.rgb *= pc.emissiveIntensity;

    if (finalColor.a < pc.alphaClipThreshold) {
        discard;
    }

    if (pc.blendMode == 1u) {
        // Additive: premultiplied rgb, zero alpha -> src.rgb + dst
        outColor = vec4(finalColor.rgb * finalColor.a, 0.0);
    } else if (pc.blendMode == 2u) {
        // Premultiplied: straight color + real alpha (fire->smoke gradient)
        outColor = finalColor;
    } else if (pc.blendMode == 3u) {
        // Multiply (dst*src): transparent = white so soft/alpha fade to no-op
        outColor = vec4(mix(vec3(1.0), finalColor.rgb, finalColor.a), finalColor.a);
    } else {
        // Alpha: premultiplied-over (identical result to the legacy straight-alpha path)
        outColor = vec4(finalColor.rgb * finalColor.a, finalColor.a);
    }
}
