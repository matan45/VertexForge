#pragma once

#include "VFXBurstTypes.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

// VK-1500 -- pure CPU-side policy and arithmetic for VFX data channels.
//
// This header intentionally has no dependency on Vulkan or the graphics module.
// The renderer and CPU-only doctests share these helpers so compatibility gates,
// ring overflow handling, and the shader's request/particle indexing cannot drift.
namespace vfx::channel
{
    inline constexpr uint32_t REQUEST_HAS_TINT = 1u << 0u;
    inline constexpr float DIRECTION_MIN_LENGTH_SQUARED = 1.0e-8f;

    enum class CompatibilityError : uint8_t
    {
        None,
        InvalidSpawnRate,
        ContinuousEmission,
        NoBursts,
        InvalidBurstCount,
        InvalidBurstTime,
        DelayedBurst,
        InfiniteBurst,
        RepeatingBurst,
        InvalidBurstProbability,
        ProbabilisticBurst
    };

    // V1 channels reproduce only deterministic, immediate, one-shot effects.
    // The first failure is returned in authored burst order for useful diagnostics.
    inline CompatibilityError validateCompatibility(float spawnRate,
                                                    std::span<const VFXBurst> bursts)
    {
        if (!std::isfinite(spawnRate) || spawnRate < 0.0f)
            return CompatibilityError::InvalidSpawnRate;
        if (spawnRate > 0.0f)
            return CompatibilityError::ContinuousEmission;
        if (bursts.empty())
            return CompatibilityError::NoBursts;

        for (const VFXBurst& burst : bursts)
        {
            if (burst.count <= 0)
                return CompatibilityError::InvalidBurstCount;
            if (!std::isfinite(burst.time))
                return CompatibilityError::InvalidBurstTime;
            if (burst.time != 0.0f)
                return CompatibilityError::DelayedBurst;
            if (burst.cycles == 0)
                return CompatibilityError::InfiniteBurst;
            if (burst.cycles != 1)
                return CompatibilityError::RepeatingBurst;
            if (!std::isfinite(burst.probability))
                return CompatibilityError::InvalidBurstProbability;
            if (burst.probability != 1.0f)
                return CompatibilityError::ProbabilisticBurst;
        }

        return CompatibilityError::None;
    }

    enum class ParticleCountError : uint8_t
    {
        None,
        ZeroCapacity,
        NoParticles,
        OverrideIsZero,
        OverrideExceedsCapacity
    };

    struct ParticleCountResult
    {
        uint32_t particlesPerRequest = 0;
        ParticleCountError error = ParticleCountError::None;
        bool usedOverride = false;
        bool clamped = false;

        [[nodiscard]] explicit operator bool() const { return error == ParticleCountError::None; }
    };

    // With no override, sum positive authored burst counts and clamp the result to
    // the listener slice. An explicit override is strict: invalid values fail rather
    // than silently changing the caller's requested particle count.
    inline ParticleCountResult resolveParticlesPerRequest(
        std::span<const VFXBurst> bursts,
        uint32_t listenerCapacity,
        std::optional<uint32_t> explicitOverride = std::nullopt)
    {
        if (listenerCapacity == 0u)
            return {0u, ParticleCountError::ZeroCapacity, explicitOverride.has_value(), false};

        if (explicitOverride.has_value())
        {
            if (*explicitOverride == 0u)
                return {0u, ParticleCountError::OverrideIsZero, true, false};
            if (*explicitOverride > listenerCapacity)
                return {0u, ParticleCountError::OverrideExceedsCapacity, true, false};
            return {*explicitOverride, ParticleCountError::None, true, false};
        }

        uint64_t total = 0u;
        for (const VFXBurst& burst : bursts)
        {
            if (burst.count > 0)
                total += static_cast<uint32_t>(burst.count);
        }

        if (total == 0u)
            return {0u, ParticleCountError::NoParticles, false, false};

        const bool clamped = total > listenerCapacity;
        return {clamped ? listenerCapacity : static_cast<uint32_t>(total),
                ParticleCountError::None, false, clamped};
    }

    struct RequestPackingResult
    {
        uint32_t base = 0;
        uint32_t accepted = 0;
        uint64_t dropped = 0;
    };

    // Pack one listener into the remaining portion of the global request ring.
    // A cursor beyond capacity is treated as a full ring, avoiding unsigned wrap.
    inline RequestPackingResult packRequests(uint32_t cursor, uint32_t capacity,
                                             uint64_t requested)
    {
        const uint32_t base = std::min(cursor, capacity);
        const uint32_t remaining = capacity - base;
        const uint32_t accepted = static_cast<uint32_t>(std::min<uint64_t>(requested, remaining));
        return {base, accepted, requested - accepted};
    }

    // Convert a position in this frame's traversal into a stable listener index.
    // Advancing rotatingStart once per frame makes first access fair without making
    // request packing nondeterministic. Empty listener sets return index zero.
    inline uint32_t fairOrderIndex(uint32_t orderPosition, uint32_t listenerCount,
                                   uint64_t rotatingStart)
    {
        if (listenerCount == 0u)
            return 0u;
        const uint64_t start = rotatingStart % listenerCount;
        return static_cast<uint32_t>((start + (orderPosition % listenerCount)) % listenerCount);
    }

    struct ParticleCapacityResult
    {
        uint64_t acceptedRequests = 0;
        uint64_t droppedRequests = 0;
        uint64_t particlesToSpawn = 0;
    };

    // Preserve visual parity per accepted request: a request is accepted only when
    // all P particles fit. The final partial request is dropped deterministically.
    inline ParticleCapacityResult limitByParticleCapacity(uint64_t requestedRequests,
                                                          uint32_t particlesPerRequest,
                                                          uint64_t availableParticleSlots)
    {
        if (particlesPerRequest == 0u)
            return {0u, requestedRequests, 0u};

        const uint64_t maxWholeRequests = availableParticleSlots / particlesPerRequest;
        const uint64_t accepted = std::min(requestedRequests, maxWholeRequests);
        return {accepted, requestedRequests - accepted,
                accepted * static_cast<uint64_t>(particlesPerRequest)};
    }

    inline std::optional<uint32_t> checkedSpawnCount(uint64_t acceptedRequests,
                                                    uint32_t particlesPerRequest)
    {
        if (particlesPerRequest == 0u)
            return std::nullopt;
        const uint64_t count = acceptedRequests * static_cast<uint64_t>(particlesPerRequest);
        if (acceptedRequests != 0u && count / acceptedRequests != particlesPerRequest)
            return std::nullopt;
        if (count > std::numeric_limits<uint32_t>::max())
            return std::nullopt;
        return static_cast<uint32_t>(count);
    }

    inline std::optional<uint32_t> requestIndexForSpawnSlot(uint32_t spawnSlot,
                                                           uint32_t requestBase,
                                                           uint32_t particlesPerRequest)
    {
        if (particlesPerRequest == 0u)
            return std::nullopt;
        const uint32_t offset = spawnSlot / particlesPerRequest;
        if (offset > std::numeric_limits<uint32_t>::max() - requestBase)
            return std::nullopt;
        return requestBase + offset;
    }

    inline std::optional<uint32_t> subParticleIndexForSpawnSlot(uint32_t spawnSlot,
                                                               uint32_t particlesPerRequest)
    {
        if (particlesPerRequest == 0u)
            return std::nullopt;
        return spawnSlot % particlesPerRequest;
    }

    // PCG RXS-M-XS hash used by the VFX simulation shader.
    inline uint32_t hash(uint32_t value)
    {
        const uint32_t state = value * 747796405u + 2891336453u;
        const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
        return (word >> 22u) ^ word;
    }

    inline uint32_t canonicalFloatBits(float value)
    {
        if (value == 0.0f)
            return 0u; // +0 and -0 describe the same world position.
        if (std::isnan(value))
            return 0x7FC00000u; // Canonical quiet NaN for deterministic diagnostics paths.
        return std::bit_cast<uint32_t>(value);
    }

    inline uint32_t deriveRequestSeed(uint64_t sequence, const glm::vec3& position)
    {
        uint32_t result = hash(static_cast<uint32_t>(sequence) ^ 0x9E3779B9u);
        result = hash(result ^ static_cast<uint32_t>(sequence >> 32u));
        result = hash(result ^ canonicalFloatBits(position.x));
        result = hash(result ^ canonicalFloatBits(position.y));
        result = hash(result ^ canonicalFloatBits(position.z));
        return result != 0u ? result : 0xA511E9B3u;
    }

    // Must remain in lockstep with the channel / gpu-child spawn branch in
    // vfx_particle_sim.glsl:
    //   seed = pcg_hash(request.seed ^ pcg_hash(subParticleIndex + 0x9E3779B9u))
    // `hash` above is the exact pcg_hash mirror (same constants), so this reproduces
    // the GPU's per-particle seed bit-for-bit. Verified by test_vfx_channel.cpp.
    inline uint32_t deriveParticleSeed(uint32_t requestSeed, uint32_t subParticleIndex)
    {
        return hash(requestSeed ^ hash(subParticleIndex + 0x9E3779B9u));
    }

    inline uint32_t packUNorm8(float value)
    {
        if (std::isnan(value) || value <= 0.0f)
            return 0u;
        if (value >= 1.0f)
            return 255u;
        return static_cast<uint32_t>(value * 255.0f + 0.5f);
    }

    // Byte order matches GLSL packUnorm4x8/unpackUnorm4x8: R occupies the low byte.
    inline uint32_t packColorRGBA8(float r, float g, float b, float a)
    {
        return packUNorm8(r) |
               (packUNorm8(g) << 8u) |
               (packUNorm8(b) << 16u) |
               (packUNorm8(a) << 24u);
    }

    inline std::array<float, 4> unpackColorRGBA8(uint32_t packed)
    {
        constexpr float inverse255 = 1.0f / 255.0f;
        return {
            static_cast<float>(packed & 0xFFu) * inverse255,
            static_cast<float>((packed >> 8u) & 0xFFu) * inverse255,
            static_cast<float>((packed >> 16u) & 0xFFu) * inverse255,
            static_cast<float>((packed >> 24u) & 0xFFu) * inverse255
        };
    }

    inline float sanitizeScale(float scale, float fallback = 1.0f)
    {
        if (!std::isfinite(fallback) || fallback <= 0.0f)
            fallback = 1.0f;
        return std::isfinite(scale) && scale > 0.0f ? scale : fallback;
    }

    // A zero result means "use the authored shape direction". This mirrors the
    // shader's dot(direction,direction) > 1e-8 channel override gate.
    inline glm::vec3 sanitizeDirection(const glm::vec3& direction)
    {
        if (!std::isfinite(direction.x) || !std::isfinite(direction.y) ||
            !std::isfinite(direction.z))
            return glm::vec3(0.0f);

        const float lengthSquared = glm::dot(direction, direction);
        if (!std::isfinite(lengthSquared) || lengthSquared <= DIRECTION_MIN_LENGTH_SQUARED)
            return glm::vec3(0.0f);
        return direction / std::sqrt(lengthSquared);
    }
}
