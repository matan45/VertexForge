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
    uint flags;             // Bit 0 = active
    vec2 padding;           // Alignment
};

struct GPUEmitterConfig
{
    vec4 emitDirection;     // xyz = direction, w = spread angle (radians)
    vec4 startColor;        // Initial RGBA color
    float spawnRate;        // Particles per second
    float lifetime;         // Particle lifetime in seconds
    float startSize;        // Initial size
    float startSpeed;       // Initial velocity magnitude
    uint maxParticles;      // Max particles for this emitter
    uint seed;              // Random seed (per frame)
    float deltaTime;        // Frame delta time
    float padding;          // Alignment
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

    bool wasActive = (p.flags & FLAG_ACTIVE) != 0u;
    bool isActive = wasActive;

    // Phase 1: Update existing active particles
    if (wasActive)
    {
        p.lifetime += config.deltaTime;

        if (p.lifetime >= p.maxLifetime)
        {
            p.flags &= ~FLAG_ACTIVE;
            isActive = false;
        }
        else
        {
            p.position += p.velocity * config.deltaTime;

            // Fade alpha near end of life
            float lifetimeRatio = p.lifetime / p.maxLifetime;
            if (lifetimeRatio > FADE_START)
            {
                float fadeProgress = (lifetimeRatio - FADE_START) / (1.0 - FADE_START);
                p.color.a = config.startColor.a * (1.0 - fadeProgress);
            }
        }
    }

    // Phase 2: Spawn new particles (inactive particles compete for spawn slots)
    if (isPlaying && !isActive && spawnThisFrame > 0u)
    {
        uint spawnSlot = atomicAdd(states[pc.emitterIndex].spawnCounter, 1u);

        if (spawnSlot < spawnThisFrame)
        {
            p.flags |= FLAG_ACTIVE;
            isActive = true;

            // Spawn at emitter origin
            p.position = vec3(worldTransform[3]);
            p.lifetime = 0.0;
            p.maxLifetime = config.lifetime;
            p.size = config.startSize;
            p.color = config.startColor;

            // Initial velocity with random spread
            vec3 baseDir = normalize(config.emitDirection.xyz);
            float spread = config.emitDirection.w;
            vec3 dir = randomInCone(seed, baseDir, spread);
            p.velocity = dir * config.startSpeed;

            // Apply emitter rotation
            mat3 rotation = mat3(worldTransform);
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
