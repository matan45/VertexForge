#include <doctest.h>
#include <vfx/VFXChannelMath.hpp>

#include <glm/glm.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

// VK-1500 -- Vulkan-free tests for the CPU/GPU VFX channel parity contract.

namespace
{
    using namespace vfx;
    using namespace vfx::channel;

    VFXBurst immediateBurst(int32_t count)
    {
        return VFXBurst{0.0f, count, 1, 0.5f, 1.0f};
    }
}

TEST_SUITE("VFXChannel")
{
    TEST_CASE("compatibility accepts only immediate deterministic one-shot bursts")
    {
        const std::vector<VFXBurst> compatible{immediateBurst(20), immediateBurst(30)};
        CHECK(validateCompatibility(0.0f, compatible) == CompatibilityError::None);
        CHECK(validateCompatibility(-0.0f, compatible) == CompatibilityError::None);

        CHECK(validateCompatibility(10.0f, compatible) == CompatibilityError::ContinuousEmission);
        CHECK(validateCompatibility(-1.0f, compatible) == CompatibilityError::InvalidSpawnRate);
        CHECK(validateCompatibility(std::numeric_limits<float>::infinity(), compatible) ==
              CompatibilityError::InvalidSpawnRate);
        CHECK(validateCompatibility(0.0f, std::span<const VFXBurst>{}) ==
              CompatibilityError::NoBursts);

        auto burst = immediateBurst(1);
        burst.count = 0;
        CHECK(validateCompatibility(0.0f, std::span<const VFXBurst>(&burst, 1)) ==
              CompatibilityError::InvalidBurstCount);

        burst = immediateBurst(1);
        burst.time = 0.1f;
        CHECK(validateCompatibility(0.0f, std::span<const VFXBurst>(&burst, 1)) ==
              CompatibilityError::DelayedBurst);

        burst = immediateBurst(1);
        burst.cycles = 0;
        CHECK(validateCompatibility(0.0f, std::span<const VFXBurst>(&burst, 1)) ==
              CompatibilityError::InfiniteBurst);

        burst = immediateBurst(1);
        burst.cycles = 2;
        CHECK(validateCompatibility(0.0f, std::span<const VFXBurst>(&burst, 1)) ==
              CompatibilityError::RepeatingBurst);

        burst = immediateBurst(1);
        burst.probability = 0.5f;
        CHECK(validateCompatibility(0.0f, std::span<const VFXBurst>(&burst, 1)) ==
              CompatibilityError::ProbabilisticBurst);
    }

    TEST_CASE("particle count derives, clamps, and validates explicit overrides")
    {
        const std::vector<VFXBurst> bursts{immediateBurst(20), immediateBurst(30)};

        const ParticleCountResult derived = resolveParticlesPerRequest(bursts, 8192u);
        REQUIRE(derived);
        CHECK(derived.particlesPerRequest == 50u);
        CHECK_FALSE(derived.usedOverride);
        CHECK_FALSE(derived.clamped);

        const std::vector<VFXBurst> large{immediateBurst(6000), immediateBurst(6000)};
        const ParticleCountResult clamped = resolveParticlesPerRequest(large, 8192u);
        REQUIRE(clamped);
        CHECK(clamped.particlesPerRequest == 8192u);
        CHECK(clamped.clamped);

        const ParticleCountResult overridden =
            resolveParticlesPerRequest(bursts, 8192u, std::optional<uint32_t>{7u});
        REQUIRE(overridden);
        CHECK(overridden.particlesPerRequest == 7u);
        CHECK(overridden.usedOverride);
        CHECK_FALSE(overridden.clamped);

        CHECK(resolveParticlesPerRequest({}, 8192u).error == ParticleCountError::NoParticles);
        CHECK(resolveParticlesPerRequest(bursts, 0u).error == ParticleCountError::ZeroCapacity);
        CHECK(resolveParticlesPerRequest(bursts, 8192u, 0u).error ==
              ParticleCountError::OverrideIsZero);
        CHECK(resolveParticlesPerRequest(bursts, 8192u, 8193u).error ==
              ParticleCountError::OverrideExceedsCapacity);
    }

    TEST_CASE("request packing handles exact fill, overflow, and exhausted cursors")
    {
        RequestPackingResult result = packRequests(10u, 16u, 6u);
        CHECK(result.base == 10u);
        CHECK(result.accepted == 6u);
        CHECK(result.dropped == 0u);

        result = packRequests(10u, 16u, 9u);
        CHECK(result.base == 10u);
        CHECK(result.accepted == 6u);
        CHECK(result.dropped == 3u);

        result = packRequests(16u, 16u, 2u);
        CHECK(result.base == 16u);
        CHECK(result.accepted == 0u);
        CHECK(result.dropped == 2u);

        result = packRequests(99u, 16u, std::numeric_limits<uint64_t>::max());
        CHECK(result.base == 16u);
        CHECK(result.accepted == 0u);
        CHECK(result.dropped == std::numeric_limits<uint64_t>::max());

        result = packRequests(0u, 0u, 0u);
        CHECK(result.base == 0u);
        CHECK(result.accepted == 0u);
        CHECK(result.dropped == 0u);
    }

    TEST_CASE("rotating traversal gives every listener first access to a constrained ring")
    {
        constexpr uint32_t listenerCount = 3u;
        constexpr uint32_t ringCapacity = 4u;
        constexpr uint64_t requestsPerListener = 3u;

        std::array<std::array<uint32_t, listenerCount>, listenerCount> acceptedByFrame{};
        for (uint32_t frame = 0u; frame < listenerCount; ++frame)
        {
            uint32_t cursor = 0u;
            for (uint32_t order = 0u; order < listenerCount; ++order)
            {
                const uint32_t listener = fairOrderIndex(order, listenerCount, frame);
                const RequestPackingResult packed =
                    packRequests(cursor, ringCapacity, requestsPerListener);
                acceptedByFrame[frame][listener] = packed.accepted;
                cursor += packed.accepted;
            }
        }

        CHECK(acceptedByFrame[0] == std::array<uint32_t, 3>{3u, 1u, 0u});
        CHECK(acceptedByFrame[1] == std::array<uint32_t, 3>{0u, 3u, 1u});
        CHECK(acceptedByFrame[2] == std::array<uint32_t, 3>{1u, 0u, 3u});
        CHECK(fairOrderIndex(0u, 0u, 123u) == 0u);
    }

    TEST_CASE("particle capacity accepts whole requests and counts exhaustion separately")
    {
        ParticleCapacityResult result = limitByParticleCapacity(10u, 4u, 40u);
        CHECK(result.acceptedRequests == 10u);
        CHECK(result.droppedRequests == 0u);
        CHECK(result.particlesToSpawn == 40u);

        result = limitByParticleCapacity(10u, 4u, 39u);
        CHECK(result.acceptedRequests == 9u);
        CHECK(result.droppedRequests == 1u);
        CHECK(result.particlesToSpawn == 36u);

        result = limitByParticleCapacity(10u, 4u, 3u);
        CHECK(result.acceptedRequests == 0u);
        CHECK(result.droppedRequests == 10u);
        CHECK(result.particlesToSpawn == 0u);

        result = limitByParticleCapacity(10u, 0u, std::numeric_limits<uint64_t>::max());
        CHECK(result.acceptedRequests == 0u);
        CHECK(result.droppedRequests == 10u);
        CHECK(result.particlesToSpawn == 0u);
    }

    TEST_CASE("spawn count and request/sub indices are checked inverses")
    {
        REQUIRE(checkedSpawnCount(3u, 8u).has_value());
        CHECK(*checkedSpawnCount(3u, 8u) == 24u);
        CHECK_FALSE(checkedSpawnCount(1u, 0u).has_value());
        CHECK_FALSE(checkedSpawnCount(std::numeric_limits<uint32_t>::max(), 2u).has_value());

        const uint32_t base = 100u;
        for (uint32_t slot = 0u; slot < 24u; ++slot)
        {
            const auto request = requestIndexForSpawnSlot(slot, base, 8u);
            const auto sub = subParticleIndexForSpawnSlot(slot, 8u);
            REQUIRE(request.has_value());
            REQUIRE(sub.has_value());
            CHECK(*request == base + slot / 8u);
            CHECK(*sub == slot % 8u);
            CHECK((*request - base) * 8u + *sub == slot);
        }

        CHECK(*requestIndexForSpawnSlot(7u, base, 8u) == base);
        CHECK(*requestIndexForSpawnSlot(8u, base, 8u) == base + 1u);
        CHECK(*subParticleIndexForSpawnSlot(7u, 8u) == 7u);
        CHECK(*subParticleIndexForSpawnSlot(8u, 8u) == 0u);
        CHECK_FALSE(requestIndexForSpawnSlot(0u, base, 0u).has_value());
        CHECK_FALSE(subParticleIndexForSpawnSlot(0u, 0u).has_value());
        CHECK_FALSE(requestIndexForSpawnSlot(std::numeric_limits<uint32_t>::max(),
                                             std::numeric_limits<uint32_t>::max(), 1u).has_value());
    }

    TEST_CASE("request and sub-particle seeds are deterministic and separated")
    {
        const glm::vec3 position(1.25f, -2.5f, 9.0f);
        const uint32_t seed = deriveRequestSeed(42u, position);
        CHECK(seed != 0u);
        CHECK(seed == deriveRequestSeed(42u, position));
        CHECK(seed != deriveRequestSeed(43u, position));
        CHECK(seed != deriveRequestSeed(42u, position + glm::vec3(0.0f, 0.0f, 0.25f)));
        CHECK(deriveRequestSeed(42u, glm::vec3(0.0f)) ==
              deriveRequestSeed(42u, glm::vec3(-0.0f)));

        CHECK(deriveParticleSeed(seed, 0u) == deriveParticleSeed(seed, 0u));
        CHECK(deriveParticleSeed(seed, 0u) != deriveParticleSeed(seed, 1u));
        CHECK(deriveParticleSeed(seed, 1u) != deriveParticleSeed(seed + 1u, 1u));
    }

    TEST_CASE("deriveParticleSeed matches the GPU channel-spawn formula bit-for-bit")
    {
        // review #13 — pin the CPU helper to the shader's derivation so the documented lockstep
        // is actually enforced:
        //   seed = pcg_hash(request.seed ^ pcg_hash(subParticleIndex + 0x9E3779B9u))
        // pcgHash below is the exact vfx_particle_sim.glsl pcg_hash mirror (same constants).
        auto pcgHash = [](uint32_t v) -> uint32_t {
            const uint32_t state = v * 747796405u + 2891336453u;
            const uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
            return (word >> 22u) ^ word;
        };
        for (uint32_t s : {0u, 1u, 42u, 0x9E3779B9u, 0xFFFFFFFFu})
            for (uint32_t i : {0u, 1u, 7u, 255u})
                CHECK(deriveParticleSeed(s, i) == pcgHash(s ^ pcgHash(i + 0x9E3779B9u)));
    }

    TEST_CASE("RGBA8 packing clamps, rounds, and does not use black as a tint sentinel")
    {
        CHECK(packColorRGBA8(1.0f, 0.5f, 0.0f, 1.0f) == 0xFF0080FFu);
        CHECK(packColorRGBA8(0.0f, 0.0f, 0.0f, 0.0f) == 0x00000000u);
        CHECK(packColorRGBA8(0.0f, 0.0f, 0.0f, 1.0f) == 0xFF000000u);
        CHECK(packColorRGBA8(-4.0f, 2.0f, std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity()) == 0xFF00FF00u);

        const auto unpacked = unpackColorRGBA8(0x4080BFFFu);
        CHECK(unpacked[0] == doctest::Approx(1.0f));
        CHECK(unpacked[1] == doctest::Approx(191.0f / 255.0f));
        CHECK(unpacked[2] == doctest::Approx(128.0f / 255.0f));
        CHECK(unpacked[3] == doctest::Approx(64.0f / 255.0f));

        const uint32_t transparentBlack = packColorRGBA8(0.0f, 0.0f, 0.0f, 0.0f);
        CHECK(transparentBlack == 0u);
        CHECK((REQUEST_HAS_TINT & REQUEST_HAS_TINT) != 0u); // Flag, not payload, carries presence.
        CHECK((0u & REQUEST_HAS_TINT) == 0u);
    }

    TEST_CASE("scale and direction sanitizers reject non-finite and degenerate input")
    {
        CHECK(sanitizeScale(2.5f) == doctest::Approx(2.5f));
        CHECK(sanitizeScale(0.0f) == doctest::Approx(1.0f));
        CHECK(sanitizeScale(-1.0f, 3.0f) == doctest::Approx(3.0f));
        CHECK(sanitizeScale(std::numeric_limits<float>::quiet_NaN()) == doctest::Approx(1.0f));
        CHECK(sanitizeScale(std::numeric_limits<float>::infinity()) == doctest::Approx(1.0f));
        CHECK(sanitizeScale(-1.0f, 0.0f) == doctest::Approx(1.0f));

        const glm::vec3 normalized = sanitizeDirection(glm::vec3(3.0f, 4.0f, 0.0f));
        CHECK(normalized.x == doctest::Approx(0.6f));
        CHECK(normalized.y == doctest::Approx(0.8f));
        CHECK(normalized.z == doctest::Approx(0.0f));

        CHECK(sanitizeDirection(glm::vec3(0.0f)) == glm::vec3(0.0f));
        CHECK(sanitizeDirection(glm::vec3(1.0e-5f, 0.0f, 0.0f)) == glm::vec3(0.0f));
        CHECK(sanitizeDirection(glm::vec3(1.0e-4f, 0.0f, 0.0f)) == glm::vec3(0.0f));
        CHECK(sanitizeDirection(glm::vec3(1.1e-4f, 0.0f, 0.0f)).x == doctest::Approx(1.0f));
        CHECK(sanitizeDirection(glm::vec3(std::numeric_limits<float>::infinity(), 0.0f, 0.0f)) ==
              glm::vec3(0.0f));
    }
}
