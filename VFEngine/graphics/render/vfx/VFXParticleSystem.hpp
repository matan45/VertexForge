#pragma once

#include "VFXBillboardTypes.hpp"
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
    };
}
