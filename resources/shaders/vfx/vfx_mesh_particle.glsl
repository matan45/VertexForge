#type VERTEX
#version 460 core

// Mesh vertex attributes (64-byte Vertex: pos, normal, texCoord, boneIndices, boneWeights)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
// locations 3-4 are boneIndices/boneWeights - not used for particles

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragLifetimeRatio;
layout(location = 3) out float fragViewDepth;
layout(location = 4) out vec3 fragNormal;
layout(location = 5) out vec3 fragWorldPos;
layout(location = 6) out float fragGlowIntensity;

struct GPUParticle
{
    vec3 position;
    float lifetime;
    vec3 velocity;
    float maxLifetime;
    vec4 color;
    float size;
    float rotation;
    float initialSize;
    float initialSpeed;
    uint spawnSeed;
    float glowIntensity;
    float _pad2;
    float _pad3;
};

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    float nearPlane;
    float farPlane;
    float _pad1;
    float _pad2;
} camera;

layout(std430, set = 0, binding = 2) readonly buffer ParticleBuffer {
    GPUParticle particles[];
};

struct GPUEmitterConfig
{
    vec4 emitDirection;
    vec4 startColor;
    float spawnRate;
    float lifetime;
    float startSize;
    float startSpeed;
    uint maxParticles;
    uint seed;
    float deltaTime;
    uint modifierFlags;

    vec4 colorStart;
    vec4 colorEnd;
    float sizeStartMult;
    float sizeEndMult;
    float speedStartMult;
    float speedEndMult;
    float angularVelocity;
    uint lutBaseOffset;
    uint lutChannelStride;
    uint lutFlags;

    vec4 gravityDir;
    vec4 windDir;
    vec4 windNoise;
    vec4 turbulence;
    vec4 vortexAxis;
    vec4 vortexCenter;

    vec4 shapeDimensions;
    uint shapeFlags;
    float flipbookColumns;
    float flipbookRows;
    float flipbookFrameRate;

    uint renderMode;
    float softParticleDistance;
    float stretchMultiplier;
    uint drawIndexCount;

    // Ribbon (VK-624)
    uint maxTrailPoints;
    float ribbonWidth;
    float ribbonMinDistance;

    // UV Scrolling (VK-623)
    float uvScrollSpeedU;
    float uvScrollSpeedV;
    float _uvPad1;
    float _uvPad2;
    float _uvPad3;
};

layout(std430, set = 0, binding = 3) readonly buffer EmitterConfigBuffer {
    GPUEmitterConfig configs[];
};

layout(push_constant) uniform PushConstants {
    uint emitterIndex;
    float alphaClipThreshold;
    uint blendMode;
    float glowColorR;
    float glowColorG;
    float glowColorB;
} pc;

void main() {
    uint particleIdx = gl_InstanceIndex;

    GPUParticle p = particles[particleIdx];

    // Cull dead particles
    if (p.size <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragTexCoord = vec2(0.0);
        fragColor = vec4(0.0);
        fragLifetimeRatio = 1.0;
        fragViewDepth = 0.0;
        fragNormal = vec3(0.0);
        fragWorldPos = vec3(0.0);
        fragGlowIntensity = 0.0;
        return;
    }

    GPUEmitterConfig config = configs[pc.emitterIndex];

    // Build rotation matrix from velocity direction
    vec3 forward = vec3(0.0, 1.0, 0.0);
    float speed = length(p.velocity);
    if (speed > 0.001) {
        forward = p.velocity / speed;
    }

    vec3 up = abs(forward.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, forward));
    up = cross(forward, right);

    // Apply rotation around forward axis (angular velocity roll)
    float cosR = cos(p.rotation);
    float sinR = sin(p.rotation);
    vec3 rotRight = right * cosR + up * sinR;
    vec3 rotUp = -right * sinR + up * cosR;

    mat3 rotationMatrix = mat3(rotRight, rotUp, forward);

    // Scale and transform mesh vertex
    vec3 scaledPos = inPosition * p.size;
    vec3 worldPos = p.position + rotationMatrix * scaledPos;

    gl_Position = camera.projection * camera.view * vec4(worldPos, 1.0);

    // View-space depth for soft particles
    fragViewDepth = -(camera.view * vec4(worldPos, 1.0)).z;

    // UV scrolling (VK-623)
    fragTexCoord = inTexCoord + vec2(config.uvScrollSpeedU, config.uvScrollSpeedV) * camera.time;
    fragColor = p.color;
    fragLifetimeRatio = (p.maxLifetime > 0.0) ? (p.lifetime / p.maxLifetime) : 0.0;
    fragNormal = rotationMatrix * inNormal;
    fragWorldPos = worldPos;
    fragGlowIntensity = p.glowIntensity;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragLifetimeRatio;
layout(location = 3) in float fragViewDepth;
layout(location = 4) in vec3 fragNormal;
layout(location = 5) in vec3 fragWorldPos;
layout(location = 6) in float fragGlowIntensity;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    vec3 cameraPos;
    float time;
    float nearPlane;
    float farPlane;
    float _pad1;
    float _pad2;
} camera;

layout(binding = 1) uniform sampler2D particleTexture;

struct GPUEmitterConfig
{
    vec4 emitDirection;
    vec4 startColor;
    float spawnRate;
    float lifetime;
    float startSize;
    float startSpeed;
    uint maxParticles;
    uint seed;
    float deltaTime;
    uint modifierFlags;

    vec4 colorStart;
    vec4 colorEnd;
    float sizeStartMult;
    float sizeEndMult;
    float speedStartMult;
    float speedEndMult;
    float angularVelocity;
    uint lutBaseOffset;
    uint lutChannelStride;
    uint lutFlags;

    vec4 gravityDir;
    vec4 windDir;
    vec4 windNoise;
    vec4 turbulence;
    vec4 vortexAxis;
    vec4 vortexCenter;

    vec4 shapeDimensions;
    uint shapeFlags;
    float flipbookColumns;
    float flipbookRows;
    float flipbookFrameRate;

    uint renderMode;
    float softParticleDistance;
    float stretchMultiplier;
    uint drawIndexCount;

    // Ribbon (VK-624)
    uint maxTrailPoints;
    float ribbonWidth;
    float ribbonMinDistance;
    float uvScrollSpeedU;
    float uvScrollSpeedV;
    float _uvPad1;
    float _uvPad2;
    float _uvPad3;
};

layout(std430, set = 0, binding = 3) readonly buffer EmitterConfigBuffer {
    GPUEmitterConfig configs[];
};

layout(binding = 4) uniform sampler2D sceneDepthTexture;

layout(push_constant) uniform PushConstants {
    uint emitterIndex;
    float alphaClipThreshold;
    uint blendMode;
    float glowColorR;
    float glowColorG;
    float glowColorB;
} pc;

void main() {
    vec4 texColor = texture(particleTexture, fragTexCoord);
    vec4 finalColor = texColor * fragColor;

    // Basic directional lighting for mesh particles
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.3));
    vec3 normal = normalize(fragNormal);
    float diffuse = max(dot(normal, lightDir), 0.0) * 0.6 + 0.4; // ambient (0.4) + diffuse (0.6)
    finalColor.rgb *= diffuse;

    // Soft particles: fade near scene geometry
    GPUEmitterConfig config = configs[pc.emitterIndex];
    if (config.softParticleDistance > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(sceneDepthTexture, 0));
        float rawDepth = texture(sceneDepthTexture, screenUV).r;

        float sceneLinearDepth = camera.nearPlane * camera.farPlane /
            (camera.farPlane - rawDepth * (camera.farPlane - camera.nearPlane));

        float depthDiff = sceneLinearDepth - fragViewDepth;
        float softFactor = clamp(depthDiff / config.softParticleDistance, 0.0, 1.0);
        finalColor.a *= softFactor;
    }

    // Glow: additive emissive color
    vec3 glowColor = vec3(pc.glowColorR, pc.glowColorG, pc.glowColorB);
    finalColor.rgb += glowColor * fragGlowIntensity;

    if (finalColor.a < pc.alphaClipThreshold) {
        discard;
    }

    if (pc.blendMode == 1u) {
        // Additive blend
        outColor = vec4(finalColor.rgb * finalColor.a, 0.0);
    } else {
        outColor = finalColor;
    }
}
