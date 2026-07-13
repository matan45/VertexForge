#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <cstddef>

namespace render::vfx
{
    struct alignas(16) GPUParticle
    {
        glm::vec3 position;
        float lifetime;
        glm::vec3 velocity;
        float maxLifetime;
        glm::vec4 color;
        float size;
        float rotation;
        float initialSize;
        float initialSpeed;
        uint32_t spawnSeed;
        float glowIntensity = 0.0f;
        float angularVelocity = 0.0f;
        uint32_t packedColorMult = 0;
    };
    static_assert(sizeof(GPUParticle) == 80, "GPUParticle must be 80 bytes for GPU alignment");
    static_assert(offsetof(GPUParticle, position) == 0, "GPUParticle::position offset mismatch");
    static_assert(offsetof(GPUParticle, lifetime) == 12, "GPUParticle::lifetime offset mismatch");
    static_assert(offsetof(GPUParticle, velocity) == 16, "GPUParticle::velocity offset mismatch");
    static_assert(offsetof(GPUParticle, maxLifetime) == 28, "GPUParticle::maxLifetime offset mismatch");
    static_assert(offsetof(GPUParticle, color) == 32, "GPUParticle::color offset mismatch");
    static_assert(offsetof(GPUParticle, size) == 48, "GPUParticle::size offset mismatch");
    static_assert(offsetof(GPUParticle, rotation) == 52, "GPUParticle::rotation offset mismatch");
    static_assert(offsetof(GPUParticle, initialSize) == 56, "GPUParticle::initialSize offset mismatch");
    static_assert(offsetof(GPUParticle, initialSpeed) == 60, "GPUParticle::initialSpeed offset mismatch");
    static_assert(offsetof(GPUParticle, spawnSeed) == 64, "GPUParticle::spawnSeed offset mismatch");
    static_assert(offsetof(GPUParticle, glowIntensity) == 68, "GPUParticle::glowIntensity offset mismatch");
    static_assert(offsetof(GPUParticle, angularVelocity) == 72, "GPUParticle::angularVelocity offset mismatch");
    static_assert(offsetof(GPUParticle, packedColorMult) == 76, "GPUParticle::packedColorMult offset mismatch");

    namespace ModifierFlags
    {
        inline constexpr uint32_t ColorOverLifetime = 1 << 0;
        inline constexpr uint32_t SizeOverLifetime = 1 << 1;
        inline constexpr uint32_t SpeedOverLifetime = 1 << 2;
        inline constexpr uint32_t RotationOverLifetime = 1 << 3;
        inline constexpr uint32_t GlowOverLifetime = 1 << 15;
        inline constexpr uint32_t SizeBySpeed = 1u << 22;  // VK-1473: sample size curve by normalized speed
        inline constexpr uint32_t ColorBySpeed = 1u << 23; // VK-1473: sample color gradient by normalized speed
        inline constexpr uint32_t DepthCollision = 1u << 25; // VK-1502: collide particles against last-frame scene depth
        // Bits 24, 26-31 free for future modifiers.
    }

    namespace ForceFlags
    {
        inline constexpr uint32_t Gravity = 1 << 4;
        inline constexpr uint32_t Wind = 1 << 5;
        inline constexpr uint32_t Turbulence = 1 << 6;
        inline constexpr uint32_t Vortex = 1 << 7;
        inline constexpr uint32_t Drag = 1 << 16;
        inline constexpr uint32_t Attractor = 1 << 17;
        inline constexpr uint32_t AttractorKill = 1 << 18; // kill particles that reach the attractor center
        inline constexpr uint32_t CurlNoise = 1 << 19;     // divergence-free curl noise force
        inline constexpr uint32_t KillVolume = 1 << 20;    // kill particles by plane/sphere/box predicate
    }

    namespace ShapeFlags
    {
        inline constexpr uint32_t ShapeSphere = 1 << 8;
        inline constexpr uint32_t ShapeCone = 1 << 9;
        inline constexpr uint32_t ShapeBox = 1 << 10;
        inline constexpr uint32_t ShapeTorus = 1 << 11; // VK-1525: now a real 3-D torus (was a flat circle)
        inline constexpr uint32_t EmitFromSurface = 1 << 12;
        inline constexpr uint32_t RandomDirection = 1 << 13;
        inline constexpr uint32_t ShapeRing = 1u << 14;        // VK-1525: flat ring / arc / annulus (XZ plane)
        inline constexpr uint32_t OrderedPlacement = 1u << 15; // VK-1525: draw the shape out in spawn order (opt-in)
        // ShapeFlags packs into GPUEmitterConfig::shapeFlags (a SEPARATE uint32 from modifierFlags);
        // bits 0-7 and 16-31 remain free here.
    }

    namespace FlipbookFlags
    {
        inline constexpr uint32_t RandomStart = 1 << 14;
        inline constexpr uint32_t FrameBlend = 1 << 21; // VK-1469: crossfade current->next cell
    }

    namespace RenderModeFlags
    {
        inline constexpr uint32_t Billboard = 0;
        inline constexpr uint32_t StretchedBillboard = 1;
        inline constexpr uint32_t HorizontalBillboard = 2;
        inline constexpr uint32_t MeshParticle = 3;
        inline constexpr uint32_t Ribbon = 4;
    }

    struct alignas(16) GPUEmitterConfig
    {
        glm::vec4 emitDirection;
        glm::vec4 startColor;
        float spawnRate;
        float lifetime;
        float startSize;
        float startSpeed;
        uint32_t maxParticles;
        uint32_t seed;
        float deltaTime;
        uint32_t modifierFlags;

        glm::vec4 colorStart;
        glm::vec4 colorEnd;
        float sizeStartMult;
        float sizeEndMult;
        float speedStartMult;
        float speedEndMult;
        float angularVelocity;
        uint32_t lutBaseOffset = 0;
        uint32_t lutChannelStride = 0;
        uint32_t lutFlags = 0;

        glm::vec4 gravityDir;
        glm::vec4 windDir;
        glm::vec4 windNoise;
        glm::vec4 turbulence;
        glm::vec4 vortexAxis;
        glm::vec4 vortexCenter;

        glm::vec4 shapeDimensions;
        uint32_t shapeFlags;
        float flipbookColumns = 1.0f;
        float flipbookRows = 1.0f;
        float flipbookFrameRate = 0.0f;

        uint32_t renderMode = RenderModeFlags::Billboard;
        float softParticleDistance = 0.0f;
        float stretchMultiplier = 1.0f;
        uint32_t drawIndexCount = 6;

        uint32_t maxTrailPoints = 0;
        float ribbonWidth = 1.0f;
        float ribbonMinDistance = 0.1f;

        float uvScrollSpeedU = 0.0f;
        float uvScrollSpeedV = 0.0f;
        uint32_t eventFlags = 0;
        float lifetimeThreshold = 0.5f;
        uint32_t colliderCount = 0;
        float collisionBounce = 0.5f;
        float collisionFriction = 0.1f;
        float collisionLifetimeLoss = 0.0f;
        uint32_t terrainCollisionEnabled = 0;

        // Lighting
        float lightingInfluence = 0.0f;       // 0 = unlit (default), 1 = fully lit
        int32_t normalMode = 0;               // 0 = sphere, 1 = view-aligned, 2 = mesh
        float ambientAmount = 0.3f;           // ambient light contribution
        // VK-1525: reclaimed lighting pad (offset 332). Previous-frame ordered sweep t, used together with
        // the per-frame spawnFrac to smear a batch of spawns across the sub-frame sweep (0 when not ordered).
        float orderedSweepTPrev = 0.0f;

        // Distortion
        uint32_t distortionEnabled = 0;
        float distortionStrength = 0.0f;
        float sizeVariance = 0.0f;
        float lifetimeVariance = 0.0f;

        // Spawn variance
        float speedVariance = 0.0f;
        float rotationVariance = 0.0f;          // radians
        float angularVelocityVariance = 0.0f;   // radians / second
        float colorValueVariance = 0.0f;
        float alphaVariance = 0.0f;
        float emissiveIntensity = 1.0f;
        // VK-1525: reclaimed variance pads (offsets 376/380). Ordered / path-driven spawn placement.
        float orderedJitter = 0.0f; // per-particle scatter off the on-curve point (world units, 0 = exact)
        float orderedSweepT = 0.0f; // current-frame sweep parameter t in [0,1] (CPU-computed from emitter age)

        // Forces added in VK-1465 (flags ForceFlags::Drag / Attractor / AttractorKill).
        // Two vec4s, independent lanes so both forces can be active at once.
        glm::vec4 attractorParams{0.0f};     // xyz = center (world space), w = strength
        glm::vec4 dragAttractorExtra{0.0f};  // x = drag linear, y = drag quadratic, z = attractor radius, w = attractor falloff

        // Force added in VK-1466 (flag ForceFlags::CurlNoise).
        glm::vec4 curlNoiseParams{0.0f};     // x = strength, y = frequency, z = scroll speed, w = octaves

        // Force added in VK-1467 (flag ForceFlags::KillVolume).
        glm::vec4 killVolumeParams0{0.0f};   // xyz = center, w = sphere radius
        glm::vec4 killVolumeParams1{0.0f};   // xyz = plane normal / box half extents, w = packed shape/invert/space

        // Speed ranges added in VK-1473 (SizeBySpeed / ColorBySpeed modifiers). Each by-speed
        // modifier remaps length(velocity) into [0,1] via its own [min,max] before sampling.
        glm::vec4 modifierSpeedRanges{0.0f}; // x = size speedMin, y = size speedMax, z = color speedMin, w = color speedMax

        // Mesh-particle orientation added in VK-1476. Consumed by the shared mesh
        // orientation function (vfx_mesh_orientation.glsl); only the MeshParticle
        // render mode reads these. Mode 0 (VelocityForward) is the legacy default.
        glm::vec4 meshOrientationParams{0.0f}; // xyz = axis-lock axis (world, normalized), w = spin rate (rad/s)
        uint32_t meshOrientationMode = 0;      // vfx::VFXOrientationMode (0 = VelocityForward)

        // VK-1501: GPU event->child fast path. Packs two 16-bit "child slots" (one per fast-path
        // event type) into the last free tail uint. Low half = OnDeath, high half = OnCollision.
        // Per half: bits 0-7 = child region index into the process-wide childSpawnBuffer
        // (0xFF = no fast-path child), bit 8 = inheritColor, bit 9 = inheritSize, bit 10 =
        // inheritVelocity. Presence is the sentinel test (no modifierFlags bit consumed) so bits
        // 24-31 and tail offsets 504/508 stay reserved for VK-1502. Pack/unpack helpers live in
        // utilities/vfx/VFXChildSpawn.hpp; the GLSL mirror is in vfx_gpu_types.glsl (keep in sync).
        uint32_t eventChildSlot = 0xFFFFFFFFu;

        // VK-1502: depth-buffer collision (gated by ModifierFlags::DepthCollision, bit 25). These fill the
        // last two reserved tail slots (offsets 504/508) so the struct stays exactly 512 bytes. Keep the
        // GLSL mirror in vfx_gpu_types.glsl in sync.
        float depthCollisionThickness = 0.25f;       // world-space shell depth behind the visible surface
        float depthCollisionNormalInfluence = 1.0f;  // [0,1]: 0 = camera-facing normal, 1 = depth-derived normal
    };
    static_assert(sizeof(GPUEmitterConfig) == 512, "GPUEmitterConfig must be 512 bytes for GPU alignment");
    static_assert(offsetof(GPUEmitterConfig, emitDirection) == 0, "GPUEmitterConfig::emitDirection offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, startColor) == 16, "GPUEmitterConfig::startColor offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, spawnRate) == 32, "GPUEmitterConfig::spawnRate offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, lifetime) == 36, "GPUEmitterConfig::lifetime offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, startSize) == 40, "GPUEmitterConfig::startSize offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, startSpeed) == 44, "GPUEmitterConfig::startSpeed offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, maxParticles) == 48, "GPUEmitterConfig::maxParticles offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, seed) == 52, "GPUEmitterConfig::seed offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, deltaTime) == 56, "GPUEmitterConfig::deltaTime offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, modifierFlags) == 60, "GPUEmitterConfig::modifierFlags offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, colorStart) == 64, "GPUEmitterConfig::colorStart offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, colorEnd) == 80, "GPUEmitterConfig::colorEnd offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, sizeStartMult) == 96, "GPUEmitterConfig::sizeStartMult offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, sizeEndMult) == 100, "GPUEmitterConfig::sizeEndMult offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, speedStartMult) == 104, "GPUEmitterConfig::speedStartMult offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, speedEndMult) == 108, "GPUEmitterConfig::speedEndMult offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, angularVelocity) == 112, "GPUEmitterConfig::angularVelocity offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, gravityDir) == 128, "GPUEmitterConfig::gravityDir offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, windDir) == 144, "GPUEmitterConfig::windDir offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, windNoise) == 160, "GPUEmitterConfig::windNoise offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, turbulence) == 176, "GPUEmitterConfig::turbulence offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, vortexAxis) == 192, "GPUEmitterConfig::vortexAxis offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, vortexCenter) == 208, "GPUEmitterConfig::vortexCenter offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, shapeDimensions) == 224, "GPUEmitterConfig::shapeDimensions offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, shapeFlags) == 240, "GPUEmitterConfig::shapeFlags offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, renderMode) == 256, "GPUEmitterConfig::renderMode offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, softParticleDistance) == 260, "GPUEmitterConfig::softParticleDistance offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, stretchMultiplier) == 264, "GPUEmitterConfig::stretchMultiplier offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, drawIndexCount) == 268, "GPUEmitterConfig::drawIndexCount offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, maxTrailPoints) == 272, "GPUEmitterConfig::maxTrailPoints offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, ribbonWidth) == 276, "GPUEmitterConfig::ribbonWidth offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, ribbonMinDistance) == 280, "GPUEmitterConfig::ribbonMinDistance offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, uvScrollSpeedU) == 284, "GPUEmitterConfig::uvScrollSpeedU offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, uvScrollSpeedV) == 288, "GPUEmitterConfig::uvScrollSpeedV offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, eventFlags) == 292, "GPUEmitterConfig::eventFlags offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, lifetimeThreshold) == 296, "GPUEmitterConfig::lifetimeThreshold offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, colliderCount) == 300, "GPUEmitterConfig::colliderCount offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, collisionBounce) == 304, "GPUEmitterConfig::collisionBounce offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, collisionFriction) == 308, "GPUEmitterConfig::collisionFriction offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, collisionLifetimeLoss) == 312, "GPUEmitterConfig::collisionLifetimeLoss offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, terrainCollisionEnabled) == 316, "GPUEmitterConfig::terrainCollisionEnabled offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, lightingInfluence) == 320, "GPUEmitterConfig::lightingInfluence offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, normalMode) == 324, "GPUEmitterConfig::normalMode offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, ambientAmount) == 328, "GPUEmitterConfig::ambientAmount offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, orderedSweepTPrev) == 332, "GPUEmitterConfig::orderedSweepTPrev offset mismatch"); // VK-1525
    static_assert(offsetof(GPUEmitterConfig, distortionEnabled) == 336, "GPUEmitterConfig::distortionEnabled offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, distortionStrength) == 340, "GPUEmitterConfig::distortionStrength offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, sizeVariance) == 344, "GPUEmitterConfig::sizeVariance offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, lifetimeVariance) == 348, "GPUEmitterConfig::lifetimeVariance offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, speedVariance) == 352, "GPUEmitterConfig::speedVariance offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, rotationVariance) == 356, "GPUEmitterConfig::rotationVariance offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, angularVelocityVariance) == 360, "GPUEmitterConfig::angularVelocityVariance offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, colorValueVariance) == 364, "GPUEmitterConfig::colorValueVariance offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, alphaVariance) == 368, "GPUEmitterConfig::alphaVariance offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, emissiveIntensity) == 372, "GPUEmitterConfig::emissiveIntensity offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, orderedJitter) == 376, "GPUEmitterConfig::orderedJitter offset mismatch"); // VK-1525
    static_assert(offsetof(GPUEmitterConfig, orderedSweepT) == 380, "GPUEmitterConfig::orderedSweepT offset mismatch"); // VK-1525
    static_assert(offsetof(GPUEmitterConfig, attractorParams) == 384, "GPUEmitterConfig::attractorParams offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, dragAttractorExtra) == 400, "GPUEmitterConfig::dragAttractorExtra offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, curlNoiseParams) == 416, "GPUEmitterConfig::curlNoiseParams offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, killVolumeParams0) == 432, "GPUEmitterConfig::killVolumeParams0 offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, killVolumeParams1) == 448, "GPUEmitterConfig::killVolumeParams1 offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, modifierSpeedRanges) == 464, "GPUEmitterConfig::modifierSpeedRanges offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, meshOrientationParams) == 480, "GPUEmitterConfig::meshOrientationParams offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, meshOrientationMode) == 496, "GPUEmitterConfig::meshOrientationMode offset mismatch");
    static_assert(offsetof(GPUEmitterConfig, eventChildSlot) == 500, "GPUEmitterConfig::eventChildSlot offset mismatch"); // VK-1501
    static_assert(offsetof(GPUEmitterConfig, depthCollisionThickness) == 504, "GPUEmitterConfig::depthCollisionThickness offset mismatch"); // VK-1502
    static_assert(offsetof(GPUEmitterConfig, depthCollisionNormalInfluence) == 508, "GPUEmitterConfig::depthCollisionNormalInfluence offset mismatch"); // VK-1502

    struct alignas(16) GPUEmitterState
    {
        glm::mat4 worldTransform;
        glm::mat4 prevWorldTransform;
        // xyz = emitter world velocity (units/s), w = inherit velocity ratio
        glm::vec4 emitterVelocityAndInherit{0.0f};
        uint32_t particleOffset;
        uint32_t maxParticles;
        uint32_t activeCount;
        uint32_t spawnThisFrame;
        float spawnAccumulator;
        uint32_t flags;
        uint32_t spawnCounter;
        uint32_t padding = 0;
    };
    static_assert(sizeof(GPUEmitterState) == 176, "GPUEmitterState must be 176 bytes for GPU alignment");
    static_assert(offsetof(GPUEmitterState, worldTransform) == 0, "GPUEmitterState::worldTransform offset mismatch");
    static_assert(offsetof(GPUEmitterState, prevWorldTransform) == 64, "GPUEmitterState::prevWorldTransform offset mismatch");
    static_assert(offsetof(GPUEmitterState, emitterVelocityAndInherit) == 128, "GPUEmitterState::emitterVelocityAndInherit offset mismatch");
    static_assert(offsetof(GPUEmitterState, particleOffset) == 144, "GPUEmitterState::particleOffset offset mismatch");
    static_assert(offsetof(GPUEmitterState, maxParticles) == 148, "GPUEmitterState::maxParticles offset mismatch");
    static_assert(offsetof(GPUEmitterState, activeCount) == 152, "GPUEmitterState::activeCount offset mismatch");
    static_assert(offsetof(GPUEmitterState, spawnThisFrame) == 156, "GPUEmitterState::spawnThisFrame offset mismatch");
    static_assert(offsetof(GPUEmitterState, spawnAccumulator) == 160, "GPUEmitterState::spawnAccumulator offset mismatch");
    static_assert(offsetof(GPUEmitterState, flags) == 164, "GPUEmitterState::flags offset mismatch");
    static_assert(offsetof(GPUEmitterState, spawnCounter) == 168, "GPUEmitterState::spawnCounter offset mismatch");
    static_assert(offsetof(GPUEmitterState, padding) == 172, "GPUEmitterState::padding offset mismatch");

    struct VFXDrawIndirectCommand
    {
        uint32_t indexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t  vertexOffset;
        uint32_t firstInstance;
    };
    static_assert(sizeof(VFXDrawIndirectCommand) == 20, "VFXDrawIndirectCommand must match VkDrawIndexedIndirectCommand");
    static_assert(offsetof(VFXDrawIndirectCommand, indexCount) == 0, "VFXDrawIndirectCommand::indexCount offset mismatch");
    static_assert(offsetof(VFXDrawIndirectCommand, instanceCount) == 4, "VFXDrawIndirectCommand::instanceCount offset mismatch");
    static_assert(offsetof(VFXDrawIndirectCommand, firstIndex) == 8, "VFXDrawIndirectCommand::firstIndex offset mismatch");
    static_assert(offsetof(VFXDrawIndirectCommand, vertexOffset) == 12, "VFXDrawIndirectCommand::vertexOffset offset mismatch");
    static_assert(offsetof(VFXDrawIndirectCommand, firstInstance) == 16, "VFXDrawIndirectCommand::firstInstance offset mismatch");

    namespace LUTChannel
    {
        // Baked LUT channel indices. LUT_CHANNELS is derived from Count, so adding a
        // channel here is a single-point, append-safe change: buffer size, upload stride,
        // and per-emitter offset all recompute off LUT_CHANNELS. Keep existing indices
        // stable (they are the packed channel order in VFXLUTBaker::bake()).
        enum : uint32_t
        {
            Color = 0,
            Size = 1,
            Speed = 2,
            Rotation = 3,
            Glow = 4,
            SizeBySpeed = 5,        // VK-1473 (C1) curve, sampled by normalized speed
            ColorBySpeed = 6,       // VK-1473 (C1) gradient, sampled by normalized speed
            RibbonWidth = 7,        // VK-1474 (C2) curve, sampled by normalized trail position
            RibbonTailGradient = 8, // VK-1474 (C2) gradient, sampled by normalized trail position
            Count
        };
    }

    namespace LUTFlags
    {
        inline constexpr uint32_t Color = 1u << LUTChannel::Color;
        inline constexpr uint32_t Size = 1u << LUTChannel::Size;
        inline constexpr uint32_t Speed = 1u << LUTChannel::Speed;
        inline constexpr uint32_t Rotation = 1u << LUTChannel::Rotation;
        inline constexpr uint32_t Glow = 1u << LUTChannel::Glow;
        inline constexpr uint32_t SizeBySpeed = 1u << LUTChannel::SizeBySpeed;               // VK-1473
        inline constexpr uint32_t ColorBySpeed = 1u << LUTChannel::ColorBySpeed;             // VK-1473
        inline constexpr uint32_t RibbonWidth = 1u << LUTChannel::RibbonWidth;               // VK-1474
        inline constexpr uint32_t RibbonTailGradient = 1u << LUTChannel::RibbonTailGradient; // VK-1474
    }

    struct alignas(16) GPUVFXEvent
    {
        glm::vec3 position;
        uint32_t eventType;
        glm::vec3 velocity;
        uint32_t emitterIndex;
        glm::vec3 color;
        float size;
    };
    static_assert(sizeof(GPUVFXEvent) == 48, "GPUVFXEvent must be 48 bytes for GPU alignment");
    static_assert(offsetof(GPUVFXEvent, position) == 0, "GPUVFXEvent::position offset mismatch");
    static_assert(offsetof(GPUVFXEvent, eventType) == 12, "GPUVFXEvent::eventType offset mismatch");
    static_assert(offsetof(GPUVFXEvent, velocity) == 16, "GPUVFXEvent::velocity offset mismatch");
    static_assert(offsetof(GPUVFXEvent, emitterIndex) == 28, "GPUVFXEvent::emitterIndex offset mismatch");
    static_assert(offsetof(GPUVFXEvent, color) == 32, "GPUVFXEvent::color offset mismatch");
    static_assert(offsetof(GPUVFXEvent, size) == 44, "GPUVFXEvent::size offset mismatch");

    // VK-1500: one world-space impact request consumed by a persistent channel listener.
    // This layout is mirrored by GPUVFXSpawnRequest in vfx_particle_sim.glsl (std430).
    struct alignas(16) GPUVFXSpawnRequest
    {
        glm::vec3 position;
        float scale = 1.0f;
        glm::vec3 direction{0.0f};
        uint32_t packedColor = 0;
        uint32_t seed = 0;
        uint32_t flags = 0;
        uint32_t reservedTemplate = 0;
        uint32_t pad = 0;
    };
    static_assert(sizeof(GPUVFXSpawnRequest) == 48, "GPUVFXSpawnRequest must be 48 bytes for GPU alignment");
    static_assert(offsetof(GPUVFXSpawnRequest, position) == 0, "GPUVFXSpawnRequest::position offset mismatch");
    static_assert(offsetof(GPUVFXSpawnRequest, scale) == 12, "GPUVFXSpawnRequest::scale offset mismatch");
    static_assert(offsetof(GPUVFXSpawnRequest, direction) == 16, "GPUVFXSpawnRequest::direction offset mismatch");
    static_assert(offsetof(GPUVFXSpawnRequest, packedColor) == 28, "GPUVFXSpawnRequest::packedColor offset mismatch");
    static_assert(offsetof(GPUVFXSpawnRequest, seed) == 32, "GPUVFXSpawnRequest::seed offset mismatch");
    static_assert(offsetof(GPUVFXSpawnRequest, flags) == 36, "GPUVFXSpawnRequest::flags offset mismatch");
    static_assert(offsetof(GPUVFXSpawnRequest, reservedTemplate) == 40, "GPUVFXSpawnRequest::reservedTemplate offset mismatch");
    static_assert(offsetof(GPUVFXSpawnRequest, pad) == 44, "GPUVFXSpawnRequest::pad offset mismatch");

    namespace SpawnRequestFlags
    {
        inline constexpr uint32_t HasTint = 1u << 0;
        inline constexpr uint32_t GpuValid = 1u << 1; // VK-1501: set on GPU-appended event->child requests
    }

    struct alignas(16) GPUCollider
    {
        glm::vec4 positionAndType;  // xyz=world center, w=float(type: 0=Sphere, 1=Box, 2=Capsule)
        glm::vec4 rotation;         // quaternion (x,y,z,w)
        glm::vec4 dimensions;       // Sphere: x=radius; Box: xyz=halfExtents; Capsule: x=radius, y=halfHeight
    };
    static_assert(sizeof(GPUCollider) == 48, "GPUCollider must be 48 bytes for GPU alignment");

    namespace GPUVFXConstants
    {
        inline constexpr uint32_t MAX_GPU_PARTICLES = 262144;
        inline constexpr uint32_t MAX_EMITTERS = 256;
        inline constexpr uint32_t DEFAULT_PARTICLES_PER_EMITTER = 1024;
        inline constexpr uint32_t WORKGROUP_SIZE = 64;
        inline constexpr uint32_t QUAD_INDEX_COUNT = 6;
        inline constexpr uint32_t LUT_RESOLUTION = 64;
        inline constexpr uint32_t LUT_CHANNELS = LUTChannel::Count; // VK-1473/1474: was 5, now derived (9)
        inline constexpr uint32_t MAX_TRAIL_POINTS = 256;
        inline constexpr uint32_t MAX_VFX_EVENTS_PER_FRAME = 256;
        inline constexpr uint32_t MAX_SCENE_COLLIDERS = 256;
        inline constexpr uint32_t MAX_TERRAIN_HEIGHTFIELD_BYTES = 4 * 1024 * 1024;
        inline constexpr uint32_t MAX_SPAWN_REQUESTS = 4096;
        inline constexpr uint32_t CHANNEL_PARTICLES_PER_EMITTER = 8192;
    }

    namespace EmitterFlags
    {
        inline constexpr uint32_t Playing = 1 << 0;
        inline constexpr uint32_t Looping = 1 << 1;
        inline constexpr uint32_t GPUDriven = 1 << 2;
    }

    struct alignas(16) GPUVFXCameraUBO
    {
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 cameraPos;
        float time;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        // VK-1502: the sim compute reads this struct as an SSBO for depth-buffer collision. These two
        // former pad words carry the collision state (render pipelines zero-init the struct and ignore them).
        uint32_t depthCollisionActive = 0; // 1 iff a valid last-frame depth is bound AND some emitter wants it
        uint32_t prevDepthSlot = 0;        // which prevFrameDepth[] element to sample (imageIndex % MAX_FRAMES_IN_FLIGHT)
    };
    static_assert(sizeof(GPUVFXCameraUBO) == 160, "GPUVFXCameraUBO must be 160 bytes");
    static_assert(offsetof(GPUVFXCameraUBO, view) == 0, "GPUVFXCameraUBO::view offset mismatch");
    static_assert(offsetof(GPUVFXCameraUBO, projection) == 64, "GPUVFXCameraUBO::projection offset mismatch");
    static_assert(offsetof(GPUVFXCameraUBO, cameraPos) == 128, "GPUVFXCameraUBO::cameraPos offset mismatch");
    static_assert(offsetof(GPUVFXCameraUBO, time) == 140, "GPUVFXCameraUBO::time offset mismatch");
    static_assert(offsetof(GPUVFXCameraUBO, nearPlane) == 144, "GPUVFXCameraUBO::nearPlane offset mismatch");
    static_assert(offsetof(GPUVFXCameraUBO, farPlane) == 148, "GPUVFXCameraUBO::farPlane offset mismatch");
    static_assert(offsetof(GPUVFXCameraUBO, depthCollisionActive) == 152, "GPUVFXCameraUBO::depthCollisionActive offset mismatch"); // VK-1502
    static_assert(offsetof(GPUVFXCameraUBO, prevDepthSlot) == 156, "GPUVFXCameraUBO::prevDepthSlot offset mismatch"); // VK-1502

    struct GPUVFXComputePushConstants
    {
        uint32_t emitterIndex;
        uint32_t frameNumber;
        uint32_t emitterCount;
        uint32_t channelRequestBase;
        uint32_t particlesPerRequest;
        uint32_t gpuChildRegion = 0xFFFFFFFFu; // VK-1501: child region for a GPU event->child listener dispatch; 0xFFFFFFFF = not a GPU child
    };
    static_assert(sizeof(GPUVFXComputePushConstants) == 24, "Push constants must be 24 bytes");
    static_assert(offsetof(GPUVFXComputePushConstants, emitterIndex) == 0, "GPUVFXComputePushConstants::emitterIndex offset mismatch");
    static_assert(offsetof(GPUVFXComputePushConstants, frameNumber) == 4, "GPUVFXComputePushConstants::frameNumber offset mismatch");
    static_assert(offsetof(GPUVFXComputePushConstants, emitterCount) == 8, "GPUVFXComputePushConstants::emitterCount offset mismatch");
    static_assert(offsetof(GPUVFXComputePushConstants, channelRequestBase) == 12, "GPUVFXComputePushConstants::channelRequestBase offset mismatch");
    static_assert(offsetof(GPUVFXComputePushConstants, particlesPerRequest) == 16, "GPUVFXComputePushConstants::particlesPerRequest offset mismatch");
    static_assert(offsetof(GPUVFXComputePushConstants, gpuChildRegion) == 20, "GPUVFXComputePushConstants::gpuChildRegion offset mismatch");

    struct GPUVFXBillboardPushConstants
    {
        uint32_t emitterIndex;
        float alphaClipThreshold = 0.1f;
        uint32_t blendMode = 0;
        float glowColorR = 1.0f;
        float glowColorG = 1.0f;
        float glowColorB = 1.0f;
        uint32_t textureIndex = 0; // VK-1481: bindless slot for this emitter's texture (0 = white default)
    };
    static_assert(sizeof(GPUVFXBillboardPushConstants) == 28, "GPUVFXBillboardPushConstants must be 28 bytes");
    static_assert(offsetof(GPUVFXBillboardPushConstants, textureIndex) == 24, "GPUVFXBillboardPushConstants::textureIndex offset mismatch");

    // VK-1481 Phase 2 (draw-call merge): a merged vkCmdDrawIndexedIndirect(drawCount>1) can't carry a
    // per-emitter push constant, so the per-emitter render-only data (that used to ride the push
    // constant) moves into this SSBO, indexed by emitterSlot = runBaseSlot + gl_DrawID. std430 layout;
    // all members are 4-byte scalars so the array stride is exactly 32 bytes.
    struct VFXEmitterRenderData
    {
        uint32_t textureIndex = 0;        // VK-1481 bindless slot (0 = white default)
        float alphaClipThreshold = 0.1f;
        uint32_t blendMode = 0;           // 0 Additive / 1 Alpha / 2 Premultiplied / 3 Multiply
        float glowColorR = 1.0f;
        float glowColorG = 1.0f;
        float glowColorB = 1.0f;
        float _pad0 = 0.0f;
        float _pad1 = 0.0f;
    };
    static_assert(sizeof(VFXEmitterRenderData) == 32, "VFXEmitterRenderData must be 32 bytes (std430 array stride)");

    // VK-1481 Phase 2: per-emitter render data for the merged DISTORTION pass (distortion needs a
    // distortionStrength the lit VFXEmitterRenderData does not carry). std430; 16-byte array stride.
    struct VFXDistortionRenderData
    {
        uint32_t textureIndex = 0;       // bindless slot (neutral-normal when unset)
        float distortionStrength = 0.1f;
        float _pad0 = 0.0f;
        float _pad1 = 0.0f;
    };
    static_assert(sizeof(VFXDistortionRenderData) == 16, "VFXDistortionRenderData must be 16 bytes (std430 array stride)");

    // VK-1526: per-emitter PBR material for MESH-render particles. GPUEmitterConfig is full (512B, no free
    // tail bytes), so an optional material's texture slots + scalars ride this dedicated SSBO instead,
    // indexed by pc.emitterIndex (mirrors the VFXEmitterRenderData per-emitter render-SSBO pattern). std430;
    // all members are 4-byte scalars so the array stride is exactly 64 bytes. The GLSL mirror lives in
    // vfx_gpu_types.glsl (keep field-for-field in sync). materialFlags bit 0 (HasMaterial) unset => the mesh
    // shader takes the byte-identical legacy single-.vfImage path and every other field is ignored.
    struct VFXMeshMaterialSlots
    {
        uint32_t baseColorIdx = 0;      // bindless slot for baseColor/albedo (0 = white default)
        uint32_t normalIdx = 0;         // bindless slot for normal map (neutral-normal when unset)
        uint32_t ormIdx = 0;            // bindless slot for packed ORM (R=AO, G=Rough, B=Metal); white when unset
        uint32_t emissiveIdx = 0;       // bindless slot for emissive (white when unset; gated by HasEmissive)
        uint32_t materialFlags = 0;     // MeshMaterialFlags bitfield
        float metallic = 0.0f;          // scalar fallback when no ORM map
        float roughness = 0.5f;         // scalar fallback when no ORM map
        float ao = 1.0f;                // scalar fallback when no ORM map
        float emissionStrength = 0.0f;  // emissive multiplier
        float albedoTintR = 1.0f;       // material baseColor tint (multiplied with particle color)
        float albedoTintG = 1.0f;
        float albedoTintB = 1.0f;
        float albedoTintA = 1.0f;
        float _pad0 = 0.0f;
        float _pad1 = 0.0f;
        float _pad2 = 0.0f;
    };
    static_assert(sizeof(VFXMeshMaterialSlots) == 64, "VFXMeshMaterialSlots must be 64 bytes (std430 array stride)");

    // VK-1526: bit flags packed into VFXMeshMaterialSlots::materialFlags.
    namespace MeshMaterialFlags
    {
        inline constexpr uint32_t HasMaterial = 1u << 0; // sentinel: 0 => legacy single-.vfImage path
        inline constexpr uint32_t UsesORM     = 1u << 1; // sample ormIdx for AO/roughness/metallic
        inline constexpr uint32_t HasNormal   = 1u << 2; // sample normalIdx and perturb the geometric normal
        inline constexpr uint32_t HasEmissive = 1u << 3; // sample emissiveIdx (else emissive = 0)
    }

    // Push constant for the merged draw: gl_DrawID identifies the sub-draw within the run; the shader
    // computes emitterSlot = runBaseSlot + gl_DrawID to index configs[] and the render-data SSBO.
    struct GPUVFXMergedPushConstants
    {
        uint32_t runBaseSlot = 0;
    };
    static_assert(sizeof(GPUVFXMergedPushConstants) == 4, "GPUVFXMergedPushConstants must be 4 bytes");

    struct GPUTerrainHeightfield
    {
        float worldOriginX;
        float worldOriginZ;
        float tileWorldSize;
        float vertexSpacing;
        int32_t gridCountX;
        int32_t gridCountZ;
        uint32_t verticesPerTile;
        uint32_t enabled;
    };
    static_assert(sizeof(GPUTerrainHeightfield) == 32, "GPUTerrainHeightfield must be 32 bytes");

    struct GPUVFXBufferSet
    {
        vk::Buffer particleBuffer;
        vk::Buffer configBuffer;
        vk::Buffer stateBuffer;
        vk::Buffer drawCommandBuffer;
        vk::Buffer lutBuffer;
        vk::Buffer ribbonRingBuffer;
        vk::Buffer ribbonHeadBuffer;
        vk::Buffer eventBuffer;
        vk::Buffer colliderBuffer;
        vk::Buffer terrainBuffer;
        vk::Buffer spawnRequestBuffer;
        vk::Buffer childSpawnBuffer; // VK-1501: GPU event->child request ring (binding 11)
    };

    struct VFXFlipbookPushConstants
    {
        float flipbookColumns;
        float flipbookRows;
        float alphaClipThreshold;
        uint32_t blendMode = 0;
        uint32_t renderMode = RenderModeFlags::Billboard;
        float stretchMultiplier = 1.0f;
        float glowColorR = 1.0f;
        float glowColorG = 1.0f;
        float glowColorB = 1.0f;
        float emissiveIntensity = 1.0f;
        float uvScrollSpeedU = 0.0f;
        float uvScrollSpeedV = 0.0f;
        uint32_t frameBlendMode = 0; // VK-1469: 0 = off, 1 = loop (wrap), 2 = clamp (one-shot)
    };
}
