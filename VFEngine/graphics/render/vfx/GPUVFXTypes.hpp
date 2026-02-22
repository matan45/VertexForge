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
    };
    static_assert(sizeof(GPUParticle) == 64, "GPUParticle must be 64 bytes for GPU alignment");
    static_assert(offsetof(GPUParticle, position) == 0, "GPUParticle::position offset mismatch");
    static_assert(offsetof(GPUParticle, lifetime) == 12, "GPUParticle::lifetime offset mismatch");
    static_assert(offsetof(GPUParticle, velocity) == 16, "GPUParticle::velocity offset mismatch");
    static_assert(offsetof(GPUParticle, maxLifetime) == 28, "GPUParticle::maxLifetime offset mismatch");
    static_assert(offsetof(GPUParticle, color) == 32, "GPUParticle::color offset mismatch");
    static_assert(offsetof(GPUParticle, size) == 48, "GPUParticle::size offset mismatch");
    static_assert(offsetof(GPUParticle, rotation) == 52, "GPUParticle::rotation offset mismatch");
    static_assert(offsetof(GPUParticle, initialSize) == 56, "GPUParticle::initialSize offset mismatch");
    static_assert(offsetof(GPUParticle, initialSpeed) == 60, "GPUParticle::initialSpeed offset mismatch");

    namespace ModifierFlags
    {
        inline constexpr uint32_t ColorOverLifetime = 1 << 0;
        inline constexpr uint32_t SizeOverLifetime = 1 << 1;
        inline constexpr uint32_t SpeedOverLifetime = 1 << 2;
        inline constexpr uint32_t RotationOverLifetime = 1 << 3;
    }

    namespace ForceFlags
    {
        inline constexpr uint32_t Gravity = 1 << 4;
        inline constexpr uint32_t Wind = 1 << 5;
        inline constexpr uint32_t Turbulence = 1 << 6;
        inline constexpr uint32_t Vortex = 1 << 7;
    }

    namespace ShapeFlags
    {
        inline constexpr uint32_t ShapeSphere = 1 << 8;
        inline constexpr uint32_t ShapeCone = 1 << 9;
        inline constexpr uint32_t ShapeBox = 1 << 10;
        inline constexpr uint32_t ShapeTorus = 1 << 11;
        inline constexpr uint32_t EmitFromSurface = 1 << 12;
        inline constexpr uint32_t RandomDirection = 1 << 13;
    }

    namespace FlipbookFlags
    {
        inline constexpr uint32_t RandomStart = 1 << 14;
    }

    namespace BlendFlags
    {
        inline constexpr uint32_t AdditiveBlend = 1 << 15;
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
        float flipbookColumns = 1.0f;   // VK-493: was shapePadding[0]
        float flipbookRows = 1.0f;      // VK-493: was shapePadding[1]
        float flipbookFrameRate = 0.0f;  // VK-493: was shapePadding[2]
    };
    static_assert(sizeof(GPUEmitterConfig) == 256, "GPUEmitterConfig must be 256 bytes for GPU alignment");
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

    struct alignas(16) GPUEmitterState
    {
        glm::mat4 worldTransform;
        uint32_t particleOffset;
        uint32_t maxParticles;
        uint32_t activeCount;
        uint32_t spawnThisFrame;
        float spawnAccumulator;
        uint32_t flags;
        uint32_t spawnCounter;
        uint32_t padding = 0;
    };
    static_assert(sizeof(GPUEmitterState) == 96, "GPUEmitterState must be 96 bytes for GPU alignment");
    static_assert(offsetof(GPUEmitterState, worldTransform) == 0, "GPUEmitterState::worldTransform offset mismatch");
    static_assert(offsetof(GPUEmitterState, particleOffset) == 64, "GPUEmitterState::particleOffset offset mismatch");
    static_assert(offsetof(GPUEmitterState, maxParticles) == 68, "GPUEmitterState::maxParticles offset mismatch");
    static_assert(offsetof(GPUEmitterState, activeCount) == 72, "GPUEmitterState::activeCount offset mismatch");
    static_assert(offsetof(GPUEmitterState, spawnThisFrame) == 76, "GPUEmitterState::spawnThisFrame offset mismatch");
    static_assert(offsetof(GPUEmitterState, spawnAccumulator) == 80, "GPUEmitterState::spawnAccumulator offset mismatch");
    static_assert(offsetof(GPUEmitterState, flags) == 84, "GPUEmitterState::flags offset mismatch");
    static_assert(offsetof(GPUEmitterState, spawnCounter) == 88, "GPUEmitterState::spawnCounter offset mismatch");
    static_assert(offsetof(GPUEmitterState, padding) == 92, "GPUEmitterState::padding offset mismatch");

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

    namespace LUTFlags
    {
        inline constexpr uint32_t Color = 1 << 0;
        inline constexpr uint32_t Size = 1 << 1;
        inline constexpr uint32_t Speed = 1 << 2;
        inline constexpr uint32_t Rotation = 1 << 3;
    }

    namespace GPUVFXConstants
    {
        inline constexpr uint32_t MAX_GPU_PARTICLES = 65536;
        inline constexpr uint32_t MAX_EMITTERS = 64;
        inline constexpr uint32_t DEFAULT_PARTICLES_PER_EMITTER = 1024;
        inline constexpr uint32_t WORKGROUP_SIZE = 64;
        inline constexpr uint32_t QUAD_INDEX_COUNT = 6;
        inline constexpr uint32_t LUT_RESOLUTION = 64;
        inline constexpr uint32_t LUT_CHANNELS = 4; // color, size, speed, rotation
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
    };
    static_assert(sizeof(GPUVFXCameraUBO) == 144, "GPUVFXCameraUBO must be 144 bytes");
    static_assert(offsetof(GPUVFXCameraUBO, view) == 0, "GPUVFXCameraUBO::view offset mismatch");
    static_assert(offsetof(GPUVFXCameraUBO, projection) == 64, "GPUVFXCameraUBO::projection offset mismatch");
    static_assert(offsetof(GPUVFXCameraUBO, cameraPos) == 128, "GPUVFXCameraUBO::cameraPos offset mismatch");
    static_assert(offsetof(GPUVFXCameraUBO, time) == 140, "GPUVFXCameraUBO::time offset mismatch");

    struct GPUVFXComputePushConstants
    {
        uint32_t emitterIndex;
        uint32_t frameNumber;
        uint32_t emitterCount;
    };
    static_assert(sizeof(GPUVFXComputePushConstants) == 12, "Push constants must be 12 bytes");
    static_assert(offsetof(GPUVFXComputePushConstants, emitterIndex) == 0, "GPUVFXComputePushConstants::emitterIndex offset mismatch");
    static_assert(offsetof(GPUVFXComputePushConstants, frameNumber) == 4, "GPUVFXComputePushConstants::frameNumber offset mismatch");
    static_assert(offsetof(GPUVFXComputePushConstants, emitterCount) == 8, "GPUVFXComputePushConstants::emitterCount offset mismatch");

    // Push constants for GPU billboard pipeline (VK-493)
    struct GPUVFXBillboardPushConstants
    {
        uint32_t emitterIndex;
        float alphaClipThreshold = 0.1f;
        uint32_t blendMode = 0;  // 0 = alpha blend, 1 = additive
    };

    // Push constants for CPU billboard pipeline (VK-493)
    struct VFXFlipbookPushConstants
    {
        float flipbookColumns;
        float flipbookRows;
        float alphaClipThreshold;
        uint32_t blendMode;  // 0 = alpha blend, 1 = additive
    };
}
