#type VERTEX
#version 460 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragLifetimeRatio;
layout(location = 3) out float fragViewDepth;
layout(location = 4) out float fragGlowIntensity;

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

    uint maxTrailPoints;
    float ribbonWidth;
    float ribbonMinDistance;
    float uvScrollSpeedU;
    float uvScrollSpeedV;
    uint eventFlags;
    float lifetimeThreshold;
    uint colliderCount;
    float collisionBounce;
    float collisionFriction;
    float collisionLifetimeLoss;
    uint terrainCollisionEnabled;

    // Lighting
    float lightingInfluence;
    uint normalMode;
    float ambientAmount;
    float _lightPad0;

    // Distortion
    uint distortionEnabled;
    float distortionStrength;
    float _distortionPad0;
    float _distortionPad1;
};

const uint MAX_TRAIL_POINTS_STRIDE = 256u;

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

layout(std430, set = 0, binding = 3) readonly buffer EmitterConfigBuffer {
    GPUEmitterConfig configs[];
};

layout(std430, set = 0, binding = 5) readonly buffer RibbonRingBuffer {
    uint ribbonRing[];
};

layout(std430, set = 0, binding = 6) readonly buffer RibbonHeadBuffer {
    uint ribbonHeads[];
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
    GPUEmitterConfig config = configs[pc.emitterIndex];
    uint maxTP = config.maxTrailPoints;
    uint head = ribbonHeads[pc.emitterIndex];
    uint segIdx = gl_InstanceIndex;

    uint ringBase = pc.emitterIndex * MAX_TRAIL_POINTS_STRIDE;

    // Newest point = head-1, next newest = head-2, etc.
    // segIdx 0 connects the two newest points, segIdx 1 the next pair, etc.
    uint slotA = (head - 1u - segIdx) % maxTP;
    uint slotB = (head - 2u - segIdx) % maxTP;
    uint pidxA = ribbonRing[ringBase + slotA];
    uint pidxB = ribbonRing[ringBase + slotB];

    GPUParticle pA = particles[pidxA];
    GPUParticle pB = particles[pidxB];

    // Cull dead segments
    if (pA.size <= 0.0 || pB.size <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragTexCoord = vec2(0.0);
        fragColor = vec4(0.0);
        fragLifetimeRatio = 1.0;
        fragViewDepth = 0.0;
        fragGlowIntensity = 0.0;
        return;
    }

    // inTexCoord.y selects which endpoint: 0 = pA (newer), 1 = pB (older)
    float along = inTexCoord.y;
    vec3 posA = pA.position;
    vec3 posB = pB.position;
    vec3 pos = mix(posA, posB, along);

    // Camera-facing ribbon: cross segment direction with view direction
    vec3 segDir = posB - posA;
    float segLen = length(segDir);
    if (segLen < 0.0001) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragTexCoord = vec2(0.0);
        fragColor = vec4(0.0);
        fragLifetimeRatio = 1.0;
        fragViewDepth = 0.0;
        fragGlowIntensity = 0.0;
        return;
    }
    segDir /= segLen;

    vec3 toCamera = normalize(camera.cameraPos - pos);
    vec3 right = normalize(cross(toCamera, segDir));

    float width = mix(pA.size, pB.size, along) * config.ribbonWidth;
    pos += right * inPosition.x * width;

    gl_Position = camera.projection * camera.view * vec4(pos, 1.0);

    // View-space depth for soft particles
    fragViewDepth = -(camera.view * vec4(pos, 1.0)).z;

    // UV: U = trail position (0=head, 1=tail), V = across width (0..1)
    uint totalSegments = min(head, maxTP) - 1u;
    float trailT = (totalSegments > 0u)
        ? (float(segIdx) + along) / float(totalSegments)
        : 0.0;
    fragTexCoord = vec2(trailT, inTexCoord.x + 0.5);

    fragTexCoord += vec2(config.uvScrollSpeedU, config.uvScrollSpeedV) * camera.time;

    // Interpolate color and lifetime
    fragColor = mix(pA.color, pB.color, along);
    float lifeA = (pA.maxLifetime > 0.0) ? (pA.lifetime / pA.maxLifetime) : 0.0;
    float lifeB = (pB.maxLifetime > 0.0) ? (pB.lifetime / pB.maxLifetime) : 0.0;
    fragLifetimeRatio = mix(lifeA, lifeB, along);
    fragGlowIntensity = mix(pA.glowIntensity, pB.glowIntensity, along);
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragLifetimeRatio;
layout(location = 3) in float fragViewDepth;
layout(location = 4) in float fragGlowIntensity;

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

    uint maxTrailPoints;
    float ribbonWidth;
    float ribbonMinDistance;
    float uvScrollSpeedU;
    float uvScrollSpeedV;
    uint eventFlags;
    float lifetimeThreshold;
    uint colliderCount;
    float collisionBounce;
    float collisionFriction;
    float collisionLifetimeLoss;
    uint terrainCollisionEnabled;

    // Lighting
    float lightingInfluence;
    uint normalMode;
    float ambientAmount;
    float _lightPad0;

    // Distortion
    uint distortionEnabled;
    float distortionStrength;
    float _distortionPad0;
    float _distortionPad1;
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
        // Additive: pre-multiply by alpha, output zero alpha
        outColor = vec4(finalColor.rgb * finalColor.a, 0.0);
    } else {
        outColor = finalColor;
    }
}
