#include "VFXParticleSystem.hpp"
#include <algorithm>
#include <chrono>

namespace render::vfx
{
    VFXParticleSystem::VFXParticleSystem()
        : rng(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()))
    {
        particles.resize(VFXConstants::MAX_PARTICLES);
    }

    void VFXParticleSystem::setEmitterConfig(const VFXEmitterConfig& emitterConfig)
    {
        config = emitterConfig;
    }

    void VFXParticleSystem::update(float deltaTime)
    {
        if (!playing)
        {
            return;
        }

        for (auto& particle : particles)
        {
            if (particle.active)
            {
                updateParticle(particle, deltaTime);
            }
        }

        if (config.spawnRate > 0.0f)
        {
            spawnAccumulator += deltaTime * config.spawnRate;

            while (spawnAccumulator >= 1.0f)
            {
                spawnParticle();
                spawnAccumulator -= 1.0f;
            }
        }
    }

    void VFXParticleSystem::reset()
    {
        for (auto& particle : particles)
        {
            particle.active = false;
        }
        spawnAccumulator = 0.0f;
    }

    std::vector<VFXInstanceData> VFXParticleSystem::getInstanceData() const
    {
        std::vector<VFXInstanceData> instances;
        instances.reserve(getActiveParticleCount());

        for (const auto& particle : particles)
        {
            if (particle.active)
            {
                VFXInstanceData instance{};
                instance.worldPosition = particle.position;
                instance.size = particle.size;
                instance.color = particle.color;
                instance.lifetimeRatio = particle.lifetime / particle.maxLifetime;
                instances.push_back(instance);
            }
        }

        return instances;
    }

    size_t VFXParticleSystem::getActiveParticleCount() const
    {
        return static_cast<size_t>(std::count_if(particles.begin(), particles.end(),
            [](const VFXParticle& p) { return p.active; }));
    }

    void VFXParticleSystem::spawnParticle()
    {
        VFXParticle* particle = findInactiveParticle();
        if (!particle)
        {
            return;
        }

        particle->active = true;
        particle->position = glm::vec3(0.0f);
        particle->lifetime = 0.0f;
        particle->maxLifetime = config.lifetime;
        particle->size = config.startSize;
        particle->color = config.startColor;

        glm::vec3 direction = glm::normalize(config.emitDirection);

        float spreadX = randomDist(rng) * 0.2f;
        float spreadZ = randomDist(rng) * 0.2f;
        direction.x += spreadX;
        direction.z += spreadZ;
        direction = glm::normalize(direction);

        particle->velocity = direction * config.startSpeed;
    }

    void VFXParticleSystem::updateParticle(VFXParticle& particle, float deltaTime)
    {
        particle.lifetime += deltaTime;

        if (particle.lifetime >= particle.maxLifetime)
        {
            particle.active = false;
            return;
        }

        particle.position += particle.velocity * deltaTime;

        float lifetimeRatio = particle.lifetime / particle.maxLifetime;

        float fadeStart = 0.7f;
        if (lifetimeRatio > fadeStart)
        {
            float fadeProgress = (lifetimeRatio - fadeStart) / (1.0f - fadeStart);
            particle.color.a = config.startColor.a * (1.0f - fadeProgress);
        }
    }

    VFXParticle* VFXParticleSystem::findInactiveParticle()
    {
        for (auto& particle : particles)
        {
            if (!particle.active)
            {
                return &particle;
            }
        }
        return nullptr;
    }
}
