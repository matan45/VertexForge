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
        float emissionTime = 0.0f;  // Tracks total emission time for looping control
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

        // Shape-based position generation (VK-240)
        glm::vec3 generateSpawnPosition();
        glm::vec3 generatePointPosition();
        glm::vec3 generateSpherePosition(float radius, bool surfaceOnly);
        glm::vec3 generateConePosition(float radius, float height, float angle, bool surfaceOnly);
        glm::vec3 generateBoxPosition(const glm::vec3& halfExtents, bool surfaceOnly);
        glm::vec3 generateCirclePosition(float radius, float arc, bool surfaceOnly);

        // Direction generation based on shape (VK-240)
        glm::vec3 generateDirectionFromShape(const glm::vec3& position);

        // Time accumulator for noise-based forces (VK-239)
        float timeAccumulator = 0.0f;

        // Unit distributions for shape generation (VK-240)
        std::uniform_real_distribution<float> unitDist{0.0f, 1.0f};
    };
}
