#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <cstdint>

namespace render::vfx
{
    // ============================================================================
    // GPU Particle Data (64 bytes, std430 aligned)
    // Must match GLSL struct exactly
    // ============================================================================
    struct alignas(16) GPUParticle
    {
        glm::vec3 position;     // 12 bytes - World space position
        float lifetime;         // 4 bytes  - Current age in seconds
        glm::vec3 velocity;     // 12 bytes - World space velocity
        float maxLifetime;      // 4 bytes  - Total lifetime in seconds
        glm::vec4 color;        // 16 bytes - RGBA color with alpha
        float size;             // 4 bytes  - Particle scale
        uint32_t flags;         // 4 bytes  - Bit 0 = active
        glm::vec2 padding;      // 8 bytes  - Alignment padding
    };
    static_assert(sizeof(GPUParticle) == 64, "GPUParticle must be 64 bytes for GPU alignment");

    // ============================================================================
    // GPU Emitter Configuration (64 bytes, std430 aligned)
    // Updated by CPU each frame, read by compute shader
    // ============================================================================
    struct alignas(16) GPUEmitterConfig
    {
        glm::vec4 emitDirection;    // 16 bytes - xyz = direction, w = spread angle (radians)
        glm::vec4 startColor;       // 16 bytes - Initial RGBA color
        float spawnRate;            // 4 bytes  - Particles per second
        float lifetime;             // 4 bytes  - Particle lifetime in seconds
        float startSize;            // 4 bytes  - Initial particle size
        float startSpeed;           // 4 bytes  - Initial velocity magnitude
        uint32_t maxParticles;      // 4 bytes  - Max particles for this emitter
        uint32_t seed;              // 4 bytes  - Random seed (changes each frame)
        float deltaTime;            // 4 bytes  - Frame delta time
        float padding;              // 4 bytes  - Alignment padding
    };
    static_assert(sizeof(GPUEmitterConfig) == 64, "GPUEmitterConfig must be 64 bytes for GPU alignment");

    // ============================================================================
    // GPU Emitter State (96 bytes, std430 aligned)
    // Contains world transform and runtime state
    // ============================================================================
    struct alignas(16) GPUEmitterState
    {
        glm::mat4 worldTransform;       // 64 bytes - Emitter world transform matrix
        uint32_t particleOffset;        // 4 bytes  - Offset into global particle buffer
        uint32_t maxParticles;          // 4 bytes  - Particles allocated for this emitter
        uint32_t activeCount;           // 4 bytes  - Active particles (written by compute, atomic)
        uint32_t spawnThisFrame;        // 4 bytes  - Particles to spawn this frame (set by CPU)
        float spawnAccumulator;         // 4 bytes  - Fractional spawn accumulator
        uint32_t flags;                 // 4 bytes  - Bit 0 = playing, bit 1 = looping
        uint32_t completedWorkgroups;   // 4 bytes  - Workgroup completion counter (atomic)
        uint32_t padding;               // 4 bytes  - Alignment padding
    };
    static_assert(sizeof(GPUEmitterState) == 96, "GPUEmitterState must be 96 bytes for GPU alignment");

    // ============================================================================
    // Indirect Draw Command (20 bytes)
    // Matches VkDrawIndexedIndirectCommand
    // ============================================================================
    struct VFXDrawIndirectCommand
    {
        uint32_t indexCount;        // Number of indices (6 for quad)
        uint32_t instanceCount;     // Active particle count (written by compute)
        uint32_t firstIndex;        // Starting index (0)
        int32_t  vertexOffset;      // Vertex offset (0)
        uint32_t firstInstance;     // Particle buffer offset (for gl_InstanceIndex)
    };
    static_assert(sizeof(VFXDrawIndirectCommand) == 20, "VFXDrawIndirectCommand must match VkDrawIndexedIndirectCommand");

    // ============================================================================
    // Constants
    // ============================================================================
    namespace GPUVFXConstants
    {
        inline constexpr uint32_t MAX_GPU_PARTICLES = 65536;      // Total particle budget (4MB)
        inline constexpr uint32_t MAX_EMITTERS = 64;              // Max concurrent emitters
        inline constexpr uint32_t DEFAULT_PARTICLES_PER_EMITTER = 1024;
        inline constexpr uint32_t WORKGROUP_SIZE = 64;            // Compute shader workgroup size
        inline constexpr uint32_t QUAD_INDEX_COUNT = 6;           // Indices per billboard quad
    }

    // ============================================================================
    // Emitter Flags
    // ============================================================================
    namespace EmitterFlags
    {
        inline constexpr uint32_t Playing = 1 << 0;
        inline constexpr uint32_t Looping = 1 << 1;
        inline constexpr uint32_t GPUDriven = 1 << 2;  // For fallback detection
    }

    // ============================================================================
    // Particle Flags
    // ============================================================================
    namespace ParticleFlags
    {
        inline constexpr uint32_t Active = 1 << 0;
    }

    // ============================================================================
    // GPU Camera UBO for VFX (matches existing VFXCameraUBO)
    // ============================================================================
    struct alignas(16) GPUVFXCameraUBO
    {
        glm::mat4 view;             // 64 bytes
        glm::mat4 projection;       // 64 bytes
        glm::vec3 cameraPos;        // 12 bytes
        float time;                 // 4 bytes
    };
    static_assert(sizeof(GPUVFXCameraUBO) == 144, "GPUVFXCameraUBO must be 144 bytes");

    // ============================================================================
    // Compute Shader Push Constants
    // ============================================================================
    struct GPUVFXComputePushConstants
    {
        uint32_t emitterIndex;      // Which emitter to process
        uint32_t frameNumber;       // For random seed variation
        uint32_t emitterCount;      // Total emitters (for bounds checking)
        uint32_t totalWorkgroups;   // Total workgroups dispatched for this emitter
    };
    static_assert(sizeof(GPUVFXComputePushConstants) == 16, "Push constants must be 16 bytes");
}
