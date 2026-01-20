#type COMPUTE
#version 450

// ============================================================================
// GPU VFX Particle Simulation Compute Shader
// Simulates particles entirely on GPU with SSBO storage
// ============================================================================

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// ----------------------------------------------------------------------------
// Data Structures (must match C++ GPUVFXTypes.hpp exactly)
// ----------------------------------------------------------------------------

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
    uint activeCount;           // Active particles (atomic counter)
    uint spawnThisFrame;        // Particles to spawn this frame
    float spawnAccumulator;     // Fractional spawn accumulator (unused in shader)
    uint flags;                 // Bit 0 = playing, bit 1 = looping
    uint completedWorkgroups;   // Workgroup completion counter (atomic)
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

// ----------------------------------------------------------------------------
// Buffer Bindings
// ----------------------------------------------------------------------------

// Binding 0: Particle buffer (read/write)
layout(std430, set = 0, binding = 0) buffer ParticleBuffer
{
    GPUParticle particles[];
};

// Binding 1: Emitter config buffer (read only)
layout(std430, set = 0, binding = 1) readonly buffer EmitterConfigBuffer
{
    GPUEmitterConfig configs[];
};

// Binding 2: Emitter state buffer (read/write for atomic counter)
layout(std430, set = 0, binding = 2) buffer EmitterStateBuffer
{
    GPUEmitterState states[];
};

// Binding 3: Indirect draw command buffer (write only)
layout(std430, set = 0, binding = 3) writeonly buffer DrawCommandBuffer
{
    VFXDrawIndirectCommand drawCommands[];
};

// ----------------------------------------------------------------------------
// Push Constants
// ----------------------------------------------------------------------------

layout(push_constant) uniform PushConstants
{
    uint emitterIndex;      // Which emitter to process
    uint frameNumber;       // For random seed variation
    uint emitterCount;      // Total emitters
    uint totalWorkgroups;   // Total workgroups dispatched for this emitter
} pc;

// ----------------------------------------------------------------------------
// Constants
// ----------------------------------------------------------------------------

const uint FLAG_ACTIVE = 1u;
const uint FLAG_PLAYING = 1u;
const uint FLAG_LOOPING = 2u;
const float FADE_START = 0.7;   // Start fading at 70% lifetime

// ----------------------------------------------------------------------------
// Random Number Generation (PCG)
// ----------------------------------------------------------------------------

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

// ----------------------------------------------------------------------------
// Main Entry Point
// ----------------------------------------------------------------------------

void main()
{
    uint localIdx = gl_GlobalInvocationID.x;

    // Bounds check
    if (pc.emitterIndex >= pc.emitterCount)
    {
        return;
    }

    GPUEmitterConfig config = configs[pc.emitterIndex];
    GPUEmitterState state = states[pc.emitterIndex];

    // Check if this thread is within the emitter's particle range
    if (localIdx >= state.maxParticles)
    {
        return;
    }

    // Check if emitter is playing
    bool isPlaying = (state.flags & FLAG_PLAYING) != 0u;

    // Calculate global particle index
    uint particleIdx = state.particleOffset + localIdx;
    GPUParticle p = particles[particleIdx];

    // Initialize random seed unique to this particle and frame
    uint seed = pcg_hash(particleIdx ^ (pc.frameNumber * 1000000u) ^ config.seed);

    // ========================================================================
    // Phase 1: Update existing active particles
    // ========================================================================
    if ((p.flags & FLAG_ACTIVE) != 0u)
    {
        p.lifetime += config.deltaTime;

        if (p.lifetime >= p.maxLifetime)
        {
            // Particle died
            p.flags &= ~FLAG_ACTIVE;
        }
        else
        {
            // Update position based on velocity
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

    // ========================================================================
    // Phase 2: Spawn new particles
    // Inactive particles compete for spawn slots using atomic counter
    // This ensures spawns go to any available inactive slot
    // ========================================================================
    if (isPlaying && (p.flags & FLAG_ACTIVE) == 0u && state.spawnThisFrame > 0u)
    {
        // Try to grab a spawn slot atomically
        // activeCount starts at 0 and is reset each frame, we reuse it temporarily
        // Actually we need a separate counter - let's use completedWorkgroups before it's used
        // Better: just check if we're in the spawn range based on atomic grab

        // Use spawnThisFrame as a limit - first N inactive particles to reach here spawn
        // We'll use a simple heuristic: lower indexed inactive particles get priority
        // Check if there are spawn slots remaining using a simple probability check
        // Thread with lower index has higher chance of spawning

        // Simple approach: use atomicAdd on a spawn counter (reuse completedWorkgroups temporarily)
        // No wait - that counter is used later. Let's just use the activeCount before it's accumulated

        // Simplest fix: probabilistic spawn based on spawn rate
        // If we want to spawn N particles and have M inactive, probability = N/M
        // But we don't know M easily...

        // Let's just use a rotating spawn pattern based on frame number
        // Each frame, a different set of particle indices can spawn
        uint spawnBase = (pc.frameNumber * 17u) % state.maxParticles;  // 17 is a prime for good distribution
        uint mySpawnIndex = (localIdx + state.maxParticles - spawnBase) % state.maxParticles;

        if (mySpawnIndex < state.spawnThisFrame)
        {
            // Spawn new particle
            p.flags |= FLAG_ACTIVE;

            // Spawn at emitter origin (extract translation from world transform)
            p.position = vec3(state.worldTransform[3]);

            p.lifetime = 0.0;
            p.maxLifetime = config.lifetime;
            p.size = config.startSize;
            p.color = config.startColor;

            // Calculate initial velocity with random spread
            vec3 baseDir = normalize(config.emitDirection.xyz);
            float spread = config.emitDirection.w;
            vec3 dir = randomInCone(seed, baseDir, spread);
            p.velocity = dir * config.startSpeed;

            // Apply emitter rotation to velocity (upper 3x3 of world transform)
            mat3 rotation = mat3(state.worldTransform);
            p.velocity = rotation * p.velocity;
        }
    }

    // Write particle back to buffer
    particles[particleIdx] = p;

    // ========================================================================
    // Phase 3: Write indirect draw command
    // Since cross-workgroup synchronization is complex, we simply draw ALL
    // particles and let the vertex shader filter inactive ones (it already does).
    // This is slightly less efficient but guarantees correctness.
    // ========================================================================
    if (gl_GlobalInvocationID.x == 0)
    {
        drawCommands[pc.emitterIndex].indexCount = 6u;           // Quad indices
        drawCommands[pc.emitterIndex].instanceCount = state.maxParticles;  // Draw all, VS filters
        drawCommands[pc.emitterIndex].firstIndex = 0u;
        drawCommands[pc.emitterIndex].vertexOffset = 0;
        drawCommands[pc.emitterIndex].firstInstance = state.particleOffset;
    }
}
