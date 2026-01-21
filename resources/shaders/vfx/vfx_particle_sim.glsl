#type COMPUTE
#version 450

// Must match GPUVFXConstants::WORKGROUP_SIZE in GPUVFXTypes.hpp
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Must match C++ GPUVFXTypes.hpp exactly
struct GPUParticle
{
    vec3 position;          // World space position
    float lifetime;         // Current age in seconds
    vec3 velocity;          // World space velocity
    float maxLifetime;      // Total lifetime in seconds
    vec4 color;             // RGBA color
    float size;             // Particle scale
    float rotation;         // VK-238: Rotation angle in radians
    float initialSize;      // VK-238: Initial size for modifier calculations
    float initialSpeed;     // VK-238: Initial speed for modifier calculations
};

// VK-238: Modifier flags (must match ModifierFlags namespace in C++)
const uint MODIFIER_COLOR_OVER_LIFETIME = 1u;
const uint MODIFIER_SIZE_OVER_LIFETIME = 2u;
const uint MODIFIER_SPEED_OVER_LIFETIME = 4u;
const uint MODIFIER_ROTATION_OVER_LIFETIME = 8u;

// VK-239: Force flags (must match ForceFlags namespace in C++)
const uint FORCE_GRAVITY = 16u;
const uint FORCE_WIND = 32u;
const uint FORCE_TURBULENCE = 64u;
const uint FORCE_VORTEX = 128u;

// VK-240: Shape flags (must match ShapeFlags namespace in C++)
const uint SHAPE_SPHERE = 256u;
const uint SHAPE_CONE = 512u;
const uint SHAPE_BOX = 1024u;
const uint SHAPE_CIRCLE = 2048u;
const uint SHAPE_EMIT_FROM_SURFACE = 4096u;
const uint SHAPE_RANDOM_DIRECTION = 8192u;

struct GPUEmitterConfig
{
    // Original fields (64 bytes)
    vec4 emitDirection;     // xyz = direction, w = spread angle (radians)
    vec4 startColor;        // Initial RGBA color
    float spawnRate;        // Particles per second
    float lifetime;         // Particle lifetime in seconds
    float startSize;        // Initial size
    float startSpeed;       // Initial velocity magnitude
    uint maxParticles;      // Max particles for this emitter
    uint seed;              // Random seed (per frame)
    float deltaTime;        // Frame delta time
    uint modifierFlags;     // VK-238: Bitmask of active modifiers

    // VK-238: Modifier data (64 bytes)
    vec4 colorStart;        // Color over lifetime start
    vec4 colorEnd;          // Color over lifetime end
    float sizeStartMult;    // Size over lifetime start multiplier
    float sizeEndMult;      // Size over lifetime end multiplier
    float speedStartMult;   // Speed over lifetime start multiplier
    float speedEndMult;     // Speed over lifetime end multiplier
    float angularVelocity;  // Rotation over lifetime (radians/sec)
    float modPadding1;
    float modPadding2;
    float modPadding3;

    // VK-239: Force data (96 bytes)
    vec4 gravityDir;        // xyz = normalized direction, w = strength
    vec4 windDir;           // xyz = direction, w = strength
    vec4 windNoise;         // x = noiseStrength, y = noiseFrequency, zw = unused
    vec4 turbulence;        // x = strength, y = frequency, z = scrollSpeed, w = octaves
    vec4 vortexAxis;        // xyz = axis, w = strength
    vec4 vortexCenter;      // xyz = center, w = radialPull

    // VK-240: Shape data (32 bytes)
    vec4 shapeDimensions;   // Sphere(r), Cone(r,h,angle), Box(hx,hy,hz), Circle(r,arc)
    uint shapeFlags;        // Shape type and emit flags
    float shapePadding1;
    float shapePadding2;
    float shapePadding3;
};

struct GPUEmitterState
{
    mat4 worldTransform;        // Emitter world transform
    uint particleOffset;        // Offset into particle buffer
    uint maxParticles;          // Allocated particles
    uint activeCount;           // Active particles (atomic counter, written by shader)
    uint spawnThisFrame;        // Particles to spawn this frame (set by CPU)
    float spawnAccumulator;     // Fractional spawn accumulator (unused in shader)
    uint flags;                 // Bit 0 = playing, bit 1 = looping
    uint spawnCounter;          // Atomic counter for spawn slots (reset each frame)
    uint padding;               // Alignment
};

struct VFXDrawIndirectCommand
{
    uint indexCount;        // 6 for quad
    uint instanceCount;     // Active particle count
    uint firstIndex;        // 0
    int vertexOffset;       // 0
    uint firstInstance;     // Particle offset
};

layout(std430, set = 0, binding = 0) buffer ParticleBuffer {
    GPUParticle particles[];
};

layout(std430, set = 0, binding = 1) readonly buffer EmitterConfigBuffer {
    GPUEmitterConfig configs[];
};

layout(std430, set = 0, binding = 2) buffer EmitterStateBuffer {
    GPUEmitterState states[];
};

layout(std430, set = 0, binding = 3) writeonly buffer DrawCommandBuffer {
    VFXDrawIndirectCommand drawCommands[];
};

layout(push_constant) uniform PushConstants {
    uint emitterIndex;
    uint frameNumber;
    uint emitterCount;
} pc;

const uint FLAG_ACTIVE = 1u;
const uint FLAG_PLAYING = 1u;
const uint FLAG_LOOPING = 2u;
const float FADE_START = 0.7;

// PCG random number generation
uint pcg_hash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float randomFloat(inout uint seed)
{
    seed = pcg_hash(seed);
    return float(seed) / float(0xFFFFFFFFu);
}

vec3 randomInCone(inout uint seed, vec3 baseDir, float spreadAngle)
{
    // Generate random direction within a cone around baseDir
    float theta = randomFloat(seed) * 6.28318530718;  // Random angle around cone
    float phi = randomFloat(seed) * spreadAngle;       // Random angle from center

    // Create perpendicular basis
    vec3 up = abs(baseDir.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, baseDir));
    vec3 forward = normalize(cross(baseDir, right));

    // Compute offset direction
    float sinPhi = sin(phi);
    float cosPhi = cos(phi);
    vec3 offset = right * (sinPhi * cos(theta)) + forward * (sinPhi * sin(theta));

    return normalize(baseDir * cosPhi + offset);
}

// VK-240: Shape-based position generation
vec3 generateSpherePosition(inout uint seed, float radius, bool surfaceOnly)
{
    // Generate random point on unit sphere using spherical coordinates
    float theta = randomFloat(seed) * 6.28318530718;  // Azimuthal angle [0, 2π]
    float u = randomFloat(seed) * 2.0 - 1.0;          // Uniform in [-1, 1]
    float phi = acos(u);                               // Polar angle [0, π] (uniform on sphere)

    vec3 direction;
    direction.x = sin(phi) * cos(theta);
    direction.y = cos(phi);
    direction.z = sin(phi) * sin(theta);

    float r = radius;
    if (!surfaceOnly)
    {
        // Use cube root for uniform volume distribution
        r = radius * pow(randomFloat(seed), 1.0 / 3.0);
    }

    return direction * r;
}

vec3 generateConePosition(inout uint seed, float baseRadius, float height, float angle, bool surfaceOnly)
{
    // Cone with apex at origin, opening upward (+Y)
    float t = randomFloat(seed);  // Position along cone height [0, 1]

    if (!surfaceOnly)
    {
        // Volume distribution - use sqrt for uniform area distribution along height
        t = sqrt(randomFloat(seed));
    }

    float y = t * height;
    float currentRadius = t * baseRadius * tan(angle);

    // Random angle around Y axis
    float theta = randomFloat(seed) * 6.28318530718;

    float r = currentRadius;
    if (!surfaceOnly)
    {
        // Random radius within the cone at this height
        r = currentRadius * sqrt(randomFloat(seed));
    }

    return vec3(r * cos(theta), y, r * sin(theta));
}

vec3 generateBoxPosition(inout uint seed, vec3 halfExtents, bool surfaceOnly)
{
    if (!surfaceOnly)
    {
        // Volume: random point inside box
        return vec3(
            (randomFloat(seed) * 2.0 - 1.0) * halfExtents.x,
            (randomFloat(seed) * 2.0 - 1.0) * halfExtents.y,
            (randomFloat(seed) * 2.0 - 1.0) * halfExtents.z
        );
    }

    // Surface: pick random face, then random point on that face
    float areaXY = halfExtents.x * halfExtents.y;
    float areaXZ = halfExtents.x * halfExtents.z;
    float areaYZ = halfExtents.y * halfExtents.z;
    float totalArea = 2.0 * (areaXY + areaXZ + areaYZ);

    float faceSelect = randomFloat(seed) * totalArea;
    float u = randomFloat(seed) * 2.0 - 1.0;
    float v = randomFloat(seed) * 2.0 - 1.0;

    if (faceSelect < areaYZ)
        return vec3(halfExtents.x, u * halfExtents.y, v * halfExtents.z);
    faceSelect -= areaYZ;

    if (faceSelect < areaYZ)
        return vec3(-halfExtents.x, u * halfExtents.y, v * halfExtents.z);
    faceSelect -= areaYZ;

    if (faceSelect < areaXZ)
        return vec3(u * halfExtents.x, halfExtents.y, v * halfExtents.z);
    faceSelect -= areaXZ;

    if (faceSelect < areaXZ)
        return vec3(u * halfExtents.x, -halfExtents.y, v * halfExtents.z);
    faceSelect -= areaXZ;

    if (faceSelect < areaXY)
        return vec3(u * halfExtents.x, v * halfExtents.y, halfExtents.z);

    return vec3(u * halfExtents.x, v * halfExtents.y, -halfExtents.z);
}

vec3 generateCirclePosition(inout uint seed, float radius, float arc, bool surfaceOnly)
{
    // Circle on XZ plane (Y = 0)
    float theta = randomFloat(seed) * arc;  // Random angle within arc

    if (surfaceOnly)
    {
        // Edge only
        return vec3(radius * cos(theta), 0.0, radius * sin(theta));
    }

    // Disk (filled circle) - use sqrt for uniform area distribution
    float r = radius * sqrt(randomFloat(seed));
    return vec3(r * cos(theta), 0.0, r * sin(theta));
}

vec3 generateSpawnPosition(inout uint seed, GPUEmitterConfig config)
{
    bool surfaceOnly = (config.shapeFlags & SHAPE_EMIT_FROM_SURFACE) != 0u;

    if ((config.shapeFlags & SHAPE_SPHERE) != 0u)
    {
        return generateSpherePosition(seed, config.shapeDimensions.x, surfaceOnly);
    }
    else if ((config.shapeFlags & SHAPE_CONE) != 0u)
    {
        return generateConePosition(seed, config.shapeDimensions.x, config.shapeDimensions.y, config.shapeDimensions.z, surfaceOnly);
    }
    else if ((config.shapeFlags & SHAPE_BOX) != 0u)
    {
        return generateBoxPosition(seed, config.shapeDimensions.xyz, surfaceOnly);
    }
    else if ((config.shapeFlags & SHAPE_CIRCLE) != 0u)
    {
        return generateCirclePosition(seed, config.shapeDimensions.x, config.shapeDimensions.y, surfaceOnly);
    }

    // Point (default)
    return vec3(0.0);
}

vec3 generateDirectionFromShape(inout uint seed, vec3 position, GPUEmitterConfig config)
{
    // If randomDirection is true, use emit direction with spread
    if ((config.shapeFlags & SHAPE_RANDOM_DIRECTION) != 0u)
    {
        vec3 baseDir = normalize(config.emitDirection.xyz);
        float spread = config.emitDirection.w;
        return randomInCone(seed, baseDir, spread);
    }

    // Otherwise, generate direction based on shape type (surface normal)
    if ((config.shapeFlags & SHAPE_SPHERE) != 0u)
    {
        // Direction is outward from center
        float len = length(position);
        if (len > 0.001)
            return position / len;
        return vec3(0.0, 1.0, 0.0);
    }
    else if ((config.shapeFlags & SHAPE_CONE) != 0u)
    {
        // Direction is along the cone surface normal
        float angle = config.shapeDimensions.z;
        vec3 radial = vec3(position.x, 0.0, position.z);
        float radialLen = length(radial);

        if (radialLen > 0.001)
        {
            vec3 outward = radial / radialLen;
            return normalize(outward * sin(angle) + vec3(0.0, cos(angle), 0.0));
        }
        return vec3(0.0, 1.0, 0.0);
    }
    else if ((config.shapeFlags & SHAPE_BOX) != 0u)
    {
        // Direction is outward from box face
        vec3 halfExtents = config.shapeDimensions.xyz;
        vec3 absPos = abs(position);
        vec3 normalizedPos = absPos / max(halfExtents, vec3(0.001));

        if (normalizedPos.x >= normalizedPos.y && normalizedPos.x >= normalizedPos.z)
            return vec3(position.x > 0.0 ? 1.0 : -1.0, 0.0, 0.0);
        else if (normalizedPos.y >= normalizedPos.x && normalizedPos.y >= normalizedPos.z)
            return vec3(0.0, position.y > 0.0 ? 1.0 : -1.0, 0.0);
        else
            return vec3(0.0, 0.0, position.z > 0.0 ? 1.0 : -1.0);
    }
    else if ((config.shapeFlags & SHAPE_CIRCLE) != 0u)
    {
        // Direction is up (Y+) from XZ plane
        return vec3(0.0, 1.0, 0.0);
    }

    // Point (default) - use emit direction with spread
    vec3 baseDir = normalize(config.emitDirection.xyz);
    float spread = config.emitDirection.w;
    return randomInCone(seed, baseDir, spread);
}

// VK-239: Simplex noise implementation (based on Stefan Gustavson's webgl-noise)
vec3 mod289_3(vec3 x) {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec4 mod289_4(vec4 x) {
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

vec4 permute(vec4 x) {
    return mod289_4(((x * 34.0) + 1.0) * x);
}

vec4 taylorInvSqrt(vec4 r) {
    return 1.79284291400159 - 0.85373472095314 * r;
}

float simplexNoise3D(vec3 v) {
    const vec2 C = vec2(1.0 / 6.0, 1.0 / 3.0);
    const vec4 D = vec4(0.0, 0.5, 1.0, 2.0);

    vec3 i = floor(v + dot(v, C.yyy));
    vec3 x0 = v - i + dot(i, C.xxx);

    vec3 g = step(x0.yzx, x0.xyz);
    vec3 l = 1.0 - g;
    vec3 i1 = min(g.xyz, l.zxy);
    vec3 i2 = max(g.xyz, l.zxy);

    vec3 x1 = x0 - i1 + C.xxx;
    vec3 x2 = x0 - i2 + C.yyy;
    vec3 x3 = x0 - D.yyy;

    i = mod289_3(i);
    vec4 p = permute(permute(permute(
        i.z + vec4(0.0, i1.z, i2.z, 1.0))
        + i.y + vec4(0.0, i1.y, i2.y, 1.0))
        + i.x + vec4(0.0, i1.x, i2.x, 1.0));

    float n_ = 0.142857142857;
    vec3 ns = n_ * D.wyz - D.xzx;

    vec4 j = p - 49.0 * floor(p * ns.z * ns.z);

    vec4 x_ = floor(j * ns.z);
    vec4 y_ = floor(j - 7.0 * x_);

    vec4 x = x_ * ns.x + ns.yyyy;
    vec4 y = y_ * ns.x + ns.yyyy;
    vec4 h = 1.0 - abs(x) - abs(y);

    vec4 b0 = vec4(x.xy, y.xy);
    vec4 b1 = vec4(x.zw, y.zw);

    vec4 s0 = floor(b0) * 2.0 + 1.0;
    vec4 s1 = floor(b1) * 2.0 + 1.0;
    vec4 sh = -step(h, vec4(0.0));

    vec4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    vec4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

    vec3 p0 = vec3(a0.xy, h.x);
    vec3 p1 = vec3(a0.zw, h.y);
    vec3 p2 = vec3(a1.xy, h.z);
    vec3 p3 = vec3(a1.zw, h.w);

    vec4 norm = taylorInvSqrt(vec4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    vec4 m = max(0.6 - vec4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, vec4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

// VK-239: Apply forces to particle velocity
void applyForces(inout GPUParticle p, GPUEmitterConfig config, float time)
{
    vec3 totalForce = vec3(0.0);

    // Gravity
    if ((config.modifierFlags & FORCE_GRAVITY) != 0u)
    {
        totalForce += config.gravityDir.xyz * config.gravityDir.w;
    }

    // Wind
    if ((config.modifierFlags & FORCE_WIND) != 0u)
    {
        vec3 windForce = config.windDir.xyz * config.windDir.w;

        // Add noise variation if enabled
        if (config.windNoise.x > 0.0)
        {
            vec3 noisePos = p.position * config.windNoise.y + vec3(time);
            float noiseX = simplexNoise3D(noisePos);
            float noiseY = simplexNoise3D(noisePos + vec3(100.0));
            float noiseZ = simplexNoise3D(noisePos + vec3(200.0));
            windForce += vec3(noiseX, noiseY, noiseZ) * config.windNoise.x;
        }

        totalForce += windForce;
    }

    // Turbulence
    if ((config.modifierFlags & FORCE_TURBULENCE) != 0u)
    {
        vec3 noisePos = p.position * config.turbulence.y;
        noisePos += vec3(time * config.turbulence.z);

        vec3 turbForce;
        int octaves = int(config.turbulence.w);
        if (octaves <= 1)
        {
            turbForce.x = simplexNoise3D(noisePos);
            turbForce.y = simplexNoise3D(noisePos + vec3(100.0));
            turbForce.z = simplexNoise3D(noisePos + vec3(200.0));
        }
        else
        {
            // FBM for richer turbulence
            float amplitude = 1.0;
            float frequency = 1.0;
            turbForce = vec3(0.0);
            for (int i = 0; i < octaves && i < 4; ++i)
            {
                vec3 samplePos = noisePos * frequency;
                turbForce.x += simplexNoise3D(samplePos) * amplitude;
                turbForce.y += simplexNoise3D(samplePos + vec3(100.0)) * amplitude;
                turbForce.z += simplexNoise3D(samplePos + vec3(200.0)) * amplitude;
                amplitude *= 0.5;
                frequency *= 2.0;
            }
        }

        totalForce += turbForce * config.turbulence.x;
    }

    // Vortex
    if ((config.modifierFlags & FORCE_VORTEX) != 0u)
    {
        vec3 toParticle = p.position - config.vortexCenter.xyz;
        vec3 axis = normalize(config.vortexAxis.xyz);

        float axisComponent = dot(toParticle, axis);
        vec3 radial = toParticle - axis * axisComponent;
        float dist = length(radial);

        if (dist > 0.001)
        {
            vec3 tangent = normalize(cross(axis, radial));
            totalForce += tangent * config.vortexAxis.w;

            // Radial pull
            if (abs(config.vortexCenter.w) > 0.001)
            {
                vec3 radialDir = normalize(radial);
                totalForce += radialDir * config.vortexCenter.w;
            }
        }
    }

    // Apply accumulated forces to velocity
    p.velocity += totalForce * config.deltaTime;
}

// VK-238: Apply modifiers based on lifetime ratio
void applyModifiers(inout GPUParticle p, GPUEmitterConfig config, float lifetimeRatio)
{
    // Color Over Lifetime
    if ((config.modifierFlags & MODIFIER_COLOR_OVER_LIFETIME) != 0u)
    {
        p.color = mix(config.colorStart, config.colorEnd, lifetimeRatio);
    }

    // Size Over Lifetime
    if ((config.modifierFlags & MODIFIER_SIZE_OVER_LIFETIME) != 0u)
    {
        float sizeMult = mix(config.sizeStartMult, config.sizeEndMult, lifetimeRatio);
        p.size = p.initialSize * sizeMult;
    }

    // Speed Over Lifetime
    if ((config.modifierFlags & MODIFIER_SPEED_OVER_LIFETIME) != 0u)
    {
        float speedMult = mix(config.speedStartMult, config.speedEndMult, lifetimeRatio);
        float currentSpeed = length(p.velocity);
        if (currentSpeed > 0.001)
        {
            vec3 dir = p.velocity / currentSpeed;
            p.velocity = dir * p.initialSpeed * speedMult;
        }
    }

    // Rotation Over Lifetime
    if ((config.modifierFlags & MODIFIER_ROTATION_OVER_LIFETIME) != 0u)
    {
        p.rotation += config.angularVelocity * config.deltaTime;
    }
}

void main()
{
    uint localIdx = gl_GlobalInvocationID.x;

    if (pc.emitterIndex >= pc.emitterCount)
        return;

    GPUEmitterConfig config = configs[pc.emitterIndex];

    // Read maxParticles directly to avoid race with state updates
    uint maxParts = states[pc.emitterIndex].maxParticles;
    if (localIdx >= maxParts)
        return;

    uint particleOffset = states[pc.emitterIndex].particleOffset;
    uint spawnThisFrame = states[pc.emitterIndex].spawnThisFrame;
    mat4 worldTransform = states[pc.emitterIndex].worldTransform;
    uint emitterFlags = states[pc.emitterIndex].flags;

    bool isPlaying = (emitterFlags & FLAG_PLAYING) != 0u;

    uint particleIdx = particleOffset + localIdx;
    GPUParticle p = particles[particleIdx];

    // Random seed unique to this particle and frame
    uint seed = pcg_hash(particleIdx ^ (pc.frameNumber * 1000000u) ^ config.seed);

    bool wasActive = (p.size > 0.0); // VK-238: Use size > 0 as active check (flags removed)
    bool isActive = wasActive;

    // Phase 1: Update existing active particles
    if (wasActive)
    {
        p.lifetime += config.deltaTime;

        if (p.lifetime >= p.maxLifetime)
        {
            p.size = 0.0; // Mark as inactive
            isActive = false;
        }
        else
        {
            // VK-239: Apply forces before position update
            float time = float(pc.frameNumber) * 0.016; // Approximate time from frame count
            applyForces(p, config, time);

            p.position += p.velocity * config.deltaTime;

            // VK-238: Apply modifiers
            float lifetimeRatio = p.lifetime / p.maxLifetime;

            if (config.modifierFlags != 0u)
            {
                applyModifiers(p, config, lifetimeRatio);
            }
            else
            {
                // Default behavior: Fade alpha near end of life
                if (lifetimeRatio > FADE_START)
                {
                    float fadeProgress = (lifetimeRatio - FADE_START) / (1.0 - FADE_START);
                    p.color.a = config.startColor.a * (1.0 - fadeProgress);
                }
            }
        }
    }

    // Phase 2: Spawn new particles (inactive particles compete for spawn slots)
    if (isPlaying && !isActive && spawnThisFrame > 0u)
    {
        uint spawnSlot = atomicAdd(states[pc.emitterIndex].spawnCounter, 1u);

        if (spawnSlot < spawnThisFrame)
        {
            isActive = true;

            // VK-240: Generate spawn position based on shape
            vec3 localPos = generateSpawnPosition(seed, config);

            // Transform to world space (position + rotation)
            mat3 rotation = mat3(worldTransform);
            p.position = vec3(worldTransform[3]) + rotation * localPos;

            p.lifetime = 0.0;
            p.maxLifetime = config.lifetime;
            p.size = config.startSize;
            p.color = config.startColor;
            p.rotation = 0.0;  // VK-238: Reset rotation

            // VK-238: Store initial values for modifier calculations
            p.initialSize = config.startSize;
            p.initialSpeed = config.startSpeed;

            // VK-240: Generate direction based on shape
            vec3 dir = generateDirectionFromShape(seed, localPos, config);
            p.velocity = dir * config.startSpeed;

            // Apply emitter rotation to velocity
            p.velocity = rotation * p.velocity;
        }
    }

    particles[particleIdx] = p;

    // Phase 3: Count active particles (for stats/debugging)
    if (isActive)
    {
        atomicAdd(states[pc.emitterIndex].activeCount, 1u);
    }

    // Phase 4: Write indirect draw command
    // Draw all allocated particles; vertex shader skips inactive ones
    // (cross-workgroup sync not possible in single dispatch)
    if (gl_GlobalInvocationID.x == 0u)
    {
        drawCommands[pc.emitterIndex].indexCount = 6u;
        drawCommands[pc.emitterIndex].instanceCount = maxParts;
        drawCommands[pc.emitterIndex].firstIndex = 0u;
        drawCommands[pc.emitterIndex].vertexOffset = 0;
        drawCommands[pc.emitterIndex].firstInstance = particleOffset;
    }
}
