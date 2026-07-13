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
    float angularVelocity;
    uint packedColorMult;
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
    float orderedSweepTPrev; // VK-1525: reclaimed pad (offset 332) - previous-frame ordered sweep t

    // Distortion
    uint distortionEnabled;
    float distortionStrength;

    // Spawn variance
    float sizeVariance;
    float lifetimeVariance;
    float speedVariance;
    float rotationVariance;
    float angularVelocityVariance;
    float colorValueVariance;
    float alphaVariance;
    float emissiveIntensity;
    float orderedJitter; // VK-1525: reclaimed pad (offset 376) - per-particle scatter off the on-curve point
    float orderedSweepT; // VK-1525: reclaimed pad (offset 380) - current-frame ordered sweep t in [0,1]

    // Forces added in VK-1465 (mirror of C++ GPUEmitterConfig).
    vec4 attractorParams;     // xyz = center (world space), w = strength
    vec4 dragAttractorExtra;  // x = drag linear, y = drag quadratic, z = attractor radius, w = attractor falloff

    // Force added in VK-1466 (mirror of C++ GPUEmitterConfig).
    vec4 curlNoiseParams;     // x = strength, y = frequency, z = scroll speed, w = octaves

    // Force added in VK-1467 (mirror of C++ GPUEmitterConfig).
    vec4 killVolumeParams0;   // xyz = center, w = sphere radius
    vec4 killVolumeParams1;   // xyz = plane normal / box half extents, w = packed shape/invert/space

    // Speed ranges added in VK-1473 (SizeBySpeed / ColorBySpeed).
    vec4 modifierSpeedRanges; // x = size speedMin, y = size speedMax, z = color speedMin, w = color speedMax

    // Mesh-particle orientation added in VK-1476 (mirror of C++ GPUEmitterConfig).
    vec4 meshOrientationParams; // xyz = axis-lock axis (world, normalized), w = spin rate (rad/s)
    uint meshOrientationMode;   // vfx::VFXOrientationMode (0 = VelocityForward)

    // VK-1501: GPU event->child fast path. Two packed 16-bit halves (low = OnDeath, high = OnCollision):
    // bits 0-7 = child region (0xFF = none), bit 8 inheritColor, bit 9 inheritSize, bit 10 inheritVelocity.
    // Mirror of C++ GPUEmitterConfig::eventChildSlot (see utilities/vfx/VFXChildSpawn.hpp).
    uint eventChildSlot;

    // VK-1502: depth-buffer collision params (reserved tail slots @504/508). Gated by MODIFIER_DEPTH_COLLISION.
    float depthCollisionThickness;      // world-space shell depth behind the visible surface
    float depthCollisionNormalInfluence; // [0,1]: 0 = camera-facing normal, 1 = depth-derived normal
};

// VK-1481 Phase 2: per-emitter render-only data read by the merged (multi-draw) VFX pass, indexed by
// emitterSlot = runBaseSlot + gl_DrawID. Mirror of C++ render::vfx::VFXEmitterRenderData (std430, 32 B).
struct VFXEmitterRenderData
{
    uint textureIndex;
    float alphaClipThreshold;
    uint blendMode;
    float glowColorR;
    float glowColorG;
    float glowColorB;
    float _pad0;
    float _pad1;
};

// VK-1481 Phase 2: per-emitter render data for the merged DISTORTION pass (mirror of C++
// render::vfx::VFXDistortionRenderData, std430, 16 B).
struct VFXDistortionRenderData
{
    uint textureIndex;
    float distortionStrength;
    float _pad0;
    float _pad1;
};
