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

    // Render mode & soft particles (VK-494)
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

const uint FLIPBOOK_RANDOM_START = (1u << 14u);

// Render mode constants (VK-494)
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
        fragLifetimeRatio = 1.0;
        fragViewDepth = 0.0;
        fragGlowIntensity = 0.0;
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
        // Stretched billboard: stretch along velocity direction (VK-494)
        float speed = length(p.velocity);
        if (speed < 0.001) {
            // Fallback to standard billboard
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
                // toCamera parallel to velDir - pick the world axis least aligned with velDir
                vec3 alt = (abs(velDir.y) < 0.999) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
                right = normalize(cross(alt, velDir));
            }

            vertexPos = p.position
                + right * rotatedPos.x * p.size
                + velDir * rotatedPos.y * p.size * config.stretchMultiplier;
        }
    } else if (config.renderMode == RENDER_MODE_HORIZONTAL) {
        // Horizontal billboard: flat on XZ plane (VK-494)
        vec3 right = vec3(1.0, 0.0, 0.0);
        vec3 forward = vec3(0.0, 0.0, 1.0);
        vertexPos = p.position
            + right * rotatedPos.x * p.size
            + forward * rotatedPos.y * p.size;
    } else {
        // Standard billboard: camera-facing
        vec3 cameraRight = vec3(camera.view[0][0], camera.view[1][0], camera.view[2][0]);
        vec3 cameraUp = vec3(camera.view[0][1], camera.view[1][1], camera.view[2][1]);
        vertexPos = p.position
            + cameraRight * rotatedPos.x * p.size
            + cameraUp * rotatedPos.y * p.size;
    }

    gl_Position = camera.projection * camera.view * vec4(vertexPos, 1.0);

    // Compute linear view-space depth for soft particles (VK-494)
    fragViewDepth = -(camera.view * vec4(vertexPos, 1.0)).z;

    float lifetimeRatio = (p.maxLifetime > 0.0) ? (p.lifetime / p.maxLifetime) : 0.0;

    float totalFrames = config.flipbookColumns * config.flipbookRows;
    float frameIndex = 0.0;
    if (totalFrames > 1.0) {
        if (config.flipbookFrameRate > 0.0)
            frameIndex = p.lifetime * config.flipbookFrameRate;
        else
            frameIndex = lifetimeRatio * totalFrames;
        if ((config.modifierFlags & FLIPBOOK_RANDOM_START) != 0u) {
            frameIndex += float(p.spawnSeed % uint(totalFrames));
        }
        frameIndex = mod(frameIndex, totalFrames);
    }
    float col = mod(floor(frameIndex), config.flipbookColumns);
    float row = floor(floor(frameIndex) / config.flipbookColumns);
    vec2 tileSize = vec2(1.0 / config.flipbookColumns, 1.0 / config.flipbookRows);
    fragTexCoord = (vec2(col, row) + inTexCoord) * tileSize;

    // UV scrolling (VK-623)
    fragTexCoord += vec2(config.uvScrollSpeedU, config.uvScrollSpeedV) * camera.time;

    fragColor = p.color;
    fragLifetimeRatio = lifetimeRatio;
    fragGlowIntensity = p.glowIntensity;
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

    // Soft particles: fade near scene geometry (VK-494)
    GPUEmitterConfig config = configs[pc.emitterIndex];
    if (config.softParticleDistance > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(textureSize(sceneDepthTexture, 0));
        float rawDepth = texture(sceneDepthTexture, screenUV).r;

        // Linearize depth (standard Vulkan depth range 0..1)
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
