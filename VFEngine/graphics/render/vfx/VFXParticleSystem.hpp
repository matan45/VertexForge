#pragma once

#include "VFXBillboardTypes.hpp"
#include "vfx/VFXModifierTypes.hpp"
#include "vfx/VFXForceTypes.hpp"
#include <vector>
#include <random>

namespace render::vfx
{
    class VFXParticleSystem
    {
    private:
        std::vector<VFXParticle> particles;
        VFXEmitterConfig config;
        float spawnAccumulator = 0.0f;
        bool playing = true;

        std::mt19937 rng;
        std::uniform_real_distribution<float> randomDist{-1.0f, 1.0f};

    public:
        explicit VFXParticleSystem();
        ~VFXParticleSystem() = default;

        void setEmitterConfig(const VFXEmitterConfig& emitterConfig);
        const VFXEmitterConfig& getEmitterConfig() const { return config; }

        void update(float deltaTime);
        void reset();

        void setPlaying(bool isPlaying) { playing = isPlaying; }
        bool isPlaying() const { return playing; }

        const std::vector<VFXParticle>& getParticles() const { return particles; }
        std::vector<VFXInstanceData> getInstanceData() const;
        size_t getActiveParticleCount() const;

    private:
        void spawnParticle();
        void updateParticle(VFXParticle& particle, float deltaTime);
        VFXParticle* findInactiveParticle();

        // Modifier application (VK-238)
        void applyModifiers(VFXParticle& particle, float lifetimeRatio, float deltaTime);
        void applyModifier(VFXParticle& particle, const ::vfx::ColorOverLifetimeConfig& mod, float t, float deltaTime);
        void applyModifier(VFXParticle& particle, const ::vfx::SizeOverLifetimeConfig& mod, float t, float deltaTime);
        void applyModifier(VFXParticle& particle, const ::vfx::SpeedOverLifetimeConfig& mod, float t, float deltaTime);
        void applyModifier(VFXParticle& particle, const ::vfx::RotationOverLifetimeConfig& mod, float t, float deltaTime);

        // Force application (VK-239)
        void applyForces(VFXParticle& particle, float deltaTime);
        void applyForce(VFXParticle& particle, const ::vfx::GravityForceConfig& force, float deltaTime);
        void applyForce(VFXParticle& particle, const ::vfx::WindForceConfig& force, float deltaTime);
        void applyForce(VFXParticle& particle, const ::vfx::TurbulenceForceConfig& force, float deltaTime);
        void applyForce(VFXParticle& particle, const ::vfx::VortexForceConfig& force, float deltaTime);

        // Time accumulator for noise-based forces (VK-239)
        float timeAccumulator = 0.0f;
    };
}
