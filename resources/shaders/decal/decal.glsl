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
    vec4 cameraParams; // x=near, y=far, z=screenWidth, w=screenHeight
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
    vec4 fadeParams;
    vec4 halfExtents;
};

layout(std430, set = 0, binding = 2) readonly buffer DecalDataBuffer {
    DecalData decals[];
};

vec3 reconstructWorldPos(vec2 screenUV, float depth)
{
    vec4 clipPos = vec4(screenUV * 2.0 - 1.0, depth, 1.0);
    vec4 worldPos = camera.inverseViewProjection * clipPos;
    return worldPos.xyz / worldPos.w;
}

void main()
{
    DecalData decal = decals[pc.decalIndex];

    vec2 screenUV = gl_FragCoord.xy / camera.cameraParams.zw;

    float depth = texture(depthTexture, screenUV).r;

    // DEBUG: Output solid red for any fragment where depth < 1.0
    // This verifies the pipeline renders and depth sampling works
    if (depth >= 1.0)
    {
        discard;
    }

    // Reconstruct world position from depth
    vec3 worldPos = reconstructWorldPos(screenUV, depth);

    // Transform into decal local space
    vec4 localPos4 = decal.inverseDecalMatrix * vec4(worldPos, 1.0);
    vec3 localPos = localPos4.xyz;

    // Check OBB containment
    vec3 absLocal = abs(localPos);
    if (absLocal.x > 1.0 || absLocal.y > 1.0 || absLocal.z > 1.0)
    {
        discard;
    }

    // Edge falloff
    float edgeFalloff = decal.fadeParams.z;
    vec3 edgeDist = 1.0 - absLocal;
    float edgeFade = smoothstep(0.0, max(edgeFalloff, 0.001), edgeDist.x)
                   * smoothstep(0.0, max(edgeFalloff, 0.001), edgeDist.y)
                   * smoothstep(0.0, max(edgeFalloff, 0.001), edgeDist.z);

    float alpha = decal.color.a * edgeFade;

    if (alpha < 0.001)
    {
        discard;
    }

    outColor = vec4(decal.color.rgb, alpha);
}
