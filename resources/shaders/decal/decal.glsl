#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
    mat4 decalWorldMatrix;
    uint decalIndex;
} pc;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 cameraParams;
} camera;

void main()
{
    vec4 worldPos = pc.decalWorldMatrix * vec4(inPosition, 1.0);
    gl_Position = camera.viewProjection * worldPos;
}

#type FRAGMENT
#version 460 core

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform PushConstants {
    mat4 decalWorldMatrix;
    uint decalIndex;
} pc;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec4 cameraParams;
} camera;

layout(set = 0, binding = 1) uniform sampler2D depthTexture;

struct DecalData {
    mat4 inverseDecalMatrix;
    vec4 color;
    vec4 fadeParams; // x=angleFadeStart, y=angleFadeEnd, z=edgeFalloff, w=normalStrength
    vec4 halfExtents; // xyz=halfExtents, w=hasAlbedoTexture (0 or 1)
};

layout(std430, set = 0, binding = 2) readonly buffer DecalDataBuffer {
    DecalData decals[];
};

layout(set = 1, binding = 0) uniform sampler2D albedoTexture;

vec3 reconstructWorldPos(vec2 screenUV, float depth)
{
    vec4 clipPos = vec4(screenUV * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = camera.inverseViewProjection * clipPos;
    return worldPos.xyz / worldPos.w;
}

vec3 reconstructNormalFromDepth(vec2 screenUV, vec2 texelSize)
{
    float depthC = texture(depthTexture, screenUV).r;
    float depthR = texture(depthTexture, screenUV + vec2(texelSize.x, 0.0)).r;
    float depthU = texture(depthTexture, screenUV + vec2(0.0, texelSize.y)).r;

    vec3 posC = reconstructWorldPos(screenUV, depthC);
    vec3 posR = reconstructWorldPos(screenUV + vec2(texelSize.x, 0.0), depthR);
    vec3 posU = reconstructWorldPos(screenUV + vec2(0.0, texelSize.y), depthU);

    vec3 tangent = posR - posC;
    vec3 bitangent = posU - posC;
    return normalize(cross(tangent, bitangent));
}

void main()
{
    DecalData decal = decals[pc.decalIndex];

    vec2 screenUV = gl_FragCoord.xy / camera.cameraParams.zw;

    float depth = texture(depthTexture, screenUV).r;

    if (depth >= 1.0)
    {
        discard;
    }

    vec3 worldPos = reconstructWorldPos(screenUV, depth);

    vec4 localPos4 = decal.inverseDecalMatrix * vec4(worldPos, 1.0);
    vec3 localPos = localPos4.xyz;

    vec3 absLocal = abs(localPos);
    if (absLocal.x > 1.0 || absLocal.y > 1.0 || absLocal.z > 1.0)
    {
        discard;
    }

    // Decal UV from local XY
    vec2 decalUV = localPos.xy * 0.5 + 0.5;

    // Sample albedo texture
    bool hasTexture = decal.halfExtents.w > 0.5;
    vec4 texColor = hasTexture ? texture(albedoTexture, decalUV) : vec4(1.0);

    vec3 finalColor = texColor.rgb * decal.color.rgb;
    float texAlpha = texColor.a;

    // Edge falloff
    float edgeFalloff = decal.fadeParams.z;
    vec3 edgeDist = 1.0 - absLocal;
    float edgeFade = smoothstep(0.0, max(edgeFalloff, 0.001), edgeDist.x)
                   * smoothstep(0.0, max(edgeFalloff, 0.001), edgeDist.y)
                   * smoothstep(0.0, max(edgeFalloff, 0.001), edgeDist.z);

    // Reconstruct surface normal from depth derivatives
    vec2 texelSize = 1.0 / camera.cameraParams.zw;
    vec3 surfaceNormal = reconstructNormalFromDepth(screenUV, texelSize);

    // Decal projection direction (local Z axis in world space)
    vec3 decalForward = normalize(vec3(pc.decalWorldMatrix[2]));

    // Angle fade: avoid stretching on steep surfaces
    float angleFadeStart = decal.fadeParams.x;
    float angleFadeEnd = decal.fadeParams.y;
    float cosAngle = abs(dot(surfaceNormal, decalForward));
    float angleFade = smoothstep(angleFadeEnd, angleFadeStart, cosAngle);

    // Normal strength: darken/lighten based on surface angle relative to decal
    // At normalStrength=0 no effect, at 1.0 full effect
    float normalStrength = decal.fadeParams.w;
    float normalShading = mix(1.0, cosAngle, normalStrength);
    finalColor *= normalShading;

    // Final alpha
    float alpha = decal.color.a * texAlpha * edgeFade * angleFade;

    if (alpha < 0.001)
    {
        discard;
    }

    outColor = vec4(finalColor, alpha);
}
