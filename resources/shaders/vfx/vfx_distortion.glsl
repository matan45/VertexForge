#type VERTEX
#version 460 core

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 fragTexCoord;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out float fragViewDepth;

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

const uint RENDER_MODE_BILLBOARD = 0u;
const uint RENDER_MODE_STRETCHED = 1u;
const uint RENDER_MODE_HORIZONTAL = 2u;

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

layout(push_constant) uniform PushConstants {
    uint emitterIndex;
} pc;

void main() {
    uint particleIdx = gl_InstanceIndex;
    GPUParticle p = particles[particleIdx];

    if (p.size <= 0.0) {
        gl_Position = vec4(0.0, 0.0, 2.0, 1.0);
        fragTexCoord = vec2(0.0);
        fragColor = vec4(0.0);
        fragViewDepth = 0.0;
        return;
    }

    GPUEmitterConfig config = configs[pc.emitterIndex];

    float cosR = cos(p.rotation);
    float sinR = sin(p.rotation);
    vec2 rotatedPos;
    rotatedPos.x = inPosition.x * cosR - inPosition.y * sinR;
    rotatedPos.y = inPosition.x * sinR + inPosition.y * cosR;

    vec3 vertexPos;

    if (config.renderMode == RENDER_MODE_STRETCHED) {
        float speed = length(p.velocity);
        if (speed < 0.001) {
            vec3 cameraRight = vec3(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
            vec3 cameraUp = vec3(camera.view[0][1], camera.view[1][1], camera.view[2][1]);
            vertexPos = p.position
                + cameraRight * rotatedPos.x * p.size
                + cameraUp * rotatedPos.y * p.size;
        } else {
            vec3 velDir = p.velocity / speed;
            vec3 toCamera = normalize(camera.cameraPos - p.position);
            vec3 rawRight = cross(toCamera, velDir);
            float rightLen = length(rawRight);
            vec3 right;
            if (rightLen > 0.001) {
                right = rawRight / rightLen;
            } else {
                vec3 alt = (abs(velDir.y) < 0.999) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
                right = normalize(cross(alt, velDir));
            }
            vertexPos = p.position
                + right * rotatedPos.x * p.size
                + velDir * rotatedPos.y * p.size * config.stretchMultiplier;
        }
    } else if (config.renderMode == RENDER_MODE_HORIZONTAL) {
        vec3 right = vec3(1.0, 0.0, 0.0);
        vec3 forward = vec3(0.0, 0.0, 1.0);
        vertexPos = p.position
            + right * rotatedPos.x * p.size
            + forward * rotatedPos.y * p.size;
    } else {
        vec3 cameraRight = vec3(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
        vec3 cameraUp = vec3(camera.view[0][1], camera.view[1][1], camera.view[2][1]);
        vertexPos = p.position
            + cameraRight * rotatedPos.x * p.size
            + cameraUp * rotatedPos.y * p.size;
    }

    gl_Position = camera.projection * camera.view * vec4(vertexPos, 1.0);
    fragViewDepth = -(camera.view * vec4(vertexPos, 1.0)).z;

    float totalFrames = config.flipbookColumns * config.flipbookRows;
    float lifetimeRatio = (p.maxLifetime > 0.0) ? (p.lifetime / p.maxLifetime) : 0.0;
    float frameIndex = 0.0;
    if (totalFrames > 1.0) {
        if (config.flipbookFrameRate > 0.0)
            frameIndex = p.lifetime * config.flipbookFrameRate;
        else
            frameIndex = lifetimeRatio * totalFrames;
        frameIndex = mod(frameIndex, totalFrames);
    }
    float col = mod(floor(frameIndex), config.flipbookColumns);
    float row = floor(floor(frameIndex) / config.flipbookColumns);
    vec2 tileSize = vec2(1.0 / config.flipbookColumns, 1.0 / config.flipbookRows);
    fragTexCoord = (vec2(col, row) + inTexCoord) * tileSize;
    fragTexCoord += vec2(config.uvScrollSpeedU, config.uvScrollSpeedV) * camera.time;

    fragColor = p.color;
}

#type FRAGMENT
#version 460 core

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in float fragViewDepth;

layout(location = 0) out vec2 outDistortion;

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

layout(binding = 1) uniform sampler2D distortionTexture;

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
    float distortionStrengthOverride; // passed via glowColorR
    float _unused1;
    float _unused2;
} pc;

void main() {
    vec4 texColor = texture(distortionTexture, fragTexCoord);

    // Interpret RG channels as distortion direction (normal map style)
    vec2 distortionDir = texColor.rg * 2.0 - 1.0;
    float mask = texColor.a * fragColor.a;

    float strength = pc.distortionStrengthOverride * mask;

    // Depth-aware fade (soft particles)
    GPUEmitterConfig config = configs[pc.emitterIndex];
    if (config.softParticleDistance > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(sceneDepthTexture, 0));
        float rawDepth = texture(sceneDepthTexture, screenUV).r;
        float sceneLinearDepth = camera.nearPlane * camera.farPlane /
            (camera.farPlane - rawDepth * (camera.farPlane - camera.nearPlane));
        float depthDiff = sceneLinearDepth - fragViewDepth;
        float softFactor = clamp(depthDiff / config.softParticleDistance, 0.0, 1.0);
        strength *= softFactor;
    }

    outDistortion = distortionDir * strength;
}
