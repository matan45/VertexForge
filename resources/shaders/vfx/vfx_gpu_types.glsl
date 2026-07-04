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
    float _lightPad0;

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
    float _variancePad1;
    float _variancePad2;

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
};
