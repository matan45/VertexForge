#type VERTEX
#version 460 core

layout(location = 0) in vec3 inPosition;

layout(push_constant) uniform PushConstants {
    mat4 decalWorldMatrix;
    uint decalIndex;
} pc;

// Matches render::common::GPUCameraData (464 bytes)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 invViewProjection;
    vec4 cameraPosition;    // xyz = pos, w = nearPlane
    vec4 screenParams;      // xy = size, zw = 1/size
    vec4 frustumPlanes[6];
    float farPlane;
    uint objectCount;
    uint hiZMipLevels;
    uint frameIndex;
    uint enableFrustumCulling;
    uint enableOcclusionCulling;
    uint enableLODSelection;
    uint batchCount;
    uint commandsPerBatch;
    uint shaderGroupCount;
    uint enableDistanceCulling;
    float globalLodBias;
    vec4 categoryDistSq0;
    vec4 categoryDistSq1;
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

// Matches render::common::GPUCameraData (464 bytes)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 invViewProjection;
    vec4 cameraPosition;    // xyz = pos, w = nearPlane
    vec4 screenParams;      // xy = size, zw = 1/size
    vec4 frustumPlanes[6];
    float farPlane;
    uint objectCount;
    uint hiZMipLevels;
    uint frameIndex;
    uint enableFrustumCulling;
    uint enableOcclusionCulling;
    uint enableLODSelection;
    uint batchCount;
    uint commandsPerBatch;
    uint shaderGroupCount;
    uint enableDistanceCulling;
    float globalLodBias;
    vec4 categoryDistSq0;
    vec4 categoryDistSq1;
} camera;

layout(set = 0, binding = 1) uniform sampler2D depthTexture;

struct DecalData {
    mat4 inverseDecalMatrix;
    vec4 color;
    vec4 fadeParams;    // x=angleFadeStart, y=angleFadeEnd, z=edgeFalloff, w=normalStrength
    vec4 textureFlags;  // x=hasAlbedo, y=hasNormal, z=hasORM, w=unused
};

layout(std430, set = 0, binding = 2) readonly buffer DecalDataBuffer {
    DecalData decals[];
};

// Per-decal textures
layout(set = 1, binding = 0) uniform sampler2D albedoTexture;
layout(set = 1, binding = 1) uniform sampler2D normalTexture;
layout(set = 1, binding = 2) uniform sampler2D ormTexture;

vec3 reconstructWorldPos(vec2 screenUV, float depth)
{
    vec4 clipPos = vec4(screenUV * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = camera.invViewProjection * clipPos;
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

    vec2 screenUV = gl_FragCoord.xy / camera.screenParams.xy;

    float depth = texture(depthTexture, screenUV).r;
    if (depth >= 1.0)
        discard;

    vec3 worldPos = reconstructWorldPos(screenUV, depth);

    vec4 localPos4 = decal.inverseDecalMatrix * vec4(worldPos, 1.0);
    vec3 localPos = localPos4.xyz;

    vec3 absLocal = abs(localPos);
    if (absLocal.x > 1.0 || absLocal.y > 1.0 || absLocal.z > 1.0)
        discard;

    // Decal UV from local XY
    vec2 decalUV = localPos.xy * 0.5 + 0.5;

    // Texture flags
    bool hasAlbedo = decal.textureFlags.x > 0.5;
    bool hasNormal = decal.textureFlags.y > 0.5;
    bool hasORM    = decal.textureFlags.z > 0.5;

    // Sample albedo
    vec4 albedoSample = hasAlbedo ? texture(albedoTexture, decalUV) : vec4(1.0);
    vec3 finalColor = albedoSample.rgb * decal.color.rgb;
    float texAlpha = albedoSample.a;

    // Sample ORM (Occlusion, Roughness, Metallic)
    vec3 ormSample = hasORM ? texture(ormTexture, decalUV).rgb : vec3(1.0, 0.5, 0.0);
    // ormSample.r = ambient occlusion, .g = roughness, .b = metallic
    // Darken decal by AO when ORM texture is present
    if (hasORM)
    {
        finalColor *= ormSample.r; // Apply AO
    }

    // Reconstruct surface normal from depth
    vec2 texelSize = camera.screenParams.zw;
    vec3 surfaceNormal = reconstructNormalFromDepth(screenUV, texelSize);

    // Decal projection direction (local Z axis in world space)
    vec3 decalForward = normalize(vec3(pc.decalWorldMatrix[2]));

    // Sample and apply normal map
    float normalStrength = decal.fadeParams.w;
    if (hasNormal && normalStrength > 0.0)
    {
        // Unpack tangent-space normal from texture
        vec3 tangentNormal = texture(normalTexture, decalUV).rgb * 2.0 - 1.0;
        tangentNormal.xy *= normalStrength;
        tangentNormal = normalize(tangentNormal);

        // Build TBN from decal's world axes
        vec3 decalRight = normalize(vec3(pc.decalWorldMatrix[0]));
        vec3 decalUp    = normalize(vec3(pc.decalWorldMatrix[1]));
        mat3 TBN = mat3(decalRight, decalUp, decalForward);

        // Perturbed world-space normal
        vec3 perturbedNormal = normalize(TBN * tangentNormal);

        // Simulate lighting from above (approximate sun direction)
        // This makes the normal map bumps clearly visible
        vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
        float diffuse = max(dot(perturbedNormal, lightDir), 0.0);
        float flatDiffuse = max(dot(surfaceNormal, lightDir), 0.0);

        // Ratio between bumped and flat lighting — this shows the normal detail
        // Values > 1 = bump catches more light, < 1 = bump is in shadow
        float bumpRatio = (diffuse + 0.1) / (flatDiffuse + 0.1);
        bumpRatio = clamp(bumpRatio, 0.5, 1.5);

        finalColor *= mix(1.0, bumpRatio, normalStrength);
    }

    // Angle fade: avoid stretching on steep surfaces
    float angleFadeStart = decal.fadeParams.x;
    float angleFadeEnd = decal.fadeParams.y;
    float cosAngle = abs(dot(surfaceNormal, decalForward));
    float angleFade = smoothstep(angleFadeEnd, angleFadeStart, cosAngle);

    // Edge falloff
    float edgeFalloff = decal.fadeParams.z;
    vec3 edgeDist = 1.0 - absLocal;
    float edgeFade = smoothstep(0.0, max(edgeFalloff, 0.001), edgeDist.x)
                   * smoothstep(0.0, max(edgeFalloff, 0.001), edgeDist.y)
                   * smoothstep(0.0, max(edgeFalloff, 0.001), edgeDist.z);

    // Final alpha
    float alpha = decal.color.a * texAlpha * edgeFade * angleFade;

    if (alpha < 0.001)
        discard;

    outColor = vec4(finalColor, alpha);
}
