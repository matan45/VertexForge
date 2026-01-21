#include "VFXParticleSystem.hpp"
#include <algorithm>
#include <chrono>
#include <glm/gtc/constants.hpp>

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
                instance.rotation = particle.rotation;
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
        particle->rotation = 0.0f;

        // Store initial values for modifier calculations (VK-238)
        particle->initialColor = config.startColor;
        particle->initialSize = config.startSize;
        particle->initialSpeed = config.startSpeed;

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

        float lifetimeRatio = particle.lifetime / particle.maxLifetime;

        // Apply modifiers (VK-238)
        if (!config.modifiers.empty())
        {
            applyModifiers(particle, lifetimeRatio, deltaTime);
        }
        else
        {
            // Default behavior when no modifiers: fade out in last 30% of lifetime
            float fadeStart = 0.7f;
            if (lifetimeRatio > fadeStart)
            {
                float fadeProgress = (lifetimeRatio - fadeStart) / (1.0f - fadeStart);
                particle.color.a = config.startColor.a * (1.0f - fadeProgress);
            }
        }

        // Update position based on (potentially modified) velocity
        particle.position += particle.velocity * deltaTime;
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

    // Modifier application (VK-238)
    void VFXParticleSystem::applyModifiers(VFXParticle& particle, float lifetimeRatio, float deltaTime)
    {
        for (const auto& modifier : config.modifiers.modifiers)
        {
            std::visit([&](const auto& mod) {
                applyModifier(particle, mod, lifetimeRatio, deltaTime);
            }, modifier);
        }
    }

    void VFXParticleSystem::applyModifier(VFXParticle& particle, const ::vfx::ColorOverLifetimeConfig& mod, float t, float /*deltaTime*/)
    {
        // Interpolate between start and end color based on lifetime ratio
        particle.color = glm::mix(mod.startColor, mod.endColor, t);
    }

    void VFXParticleSystem::applyModifier(VFXParticle& particle, const ::vfx::SizeOverLifetimeConfig& mod, float t, float /*deltaTime*/)
    {
        // Interpolate size multiplier and apply to initial size
        float multiplier = glm::mix(mod.startMultiplier, mod.endMultiplier, t);
        particle.size = particle.initialSize * multiplier;
    }

    void VFXParticleSystem::applyModifier(VFXParticle& particle, const ::vfx::SpeedOverLifetimeConfig& mod, float t, float /*deltaTime*/)
    {
        // Interpolate speed multiplier and apply to velocity
        float multiplier = glm::mix(mod.startMultiplier, mod.endMultiplier, t);
        float currentSpeed = glm::length(particle.velocity);
        if (currentSpeed > 0.001f)
        {
            glm::vec3 direction = particle.velocity / currentSpeed;
            particle.velocity = direction * particle.initialSpeed * multiplier;
        }
    }

    void VFXParticleSystem::applyModifier(VFXParticle& particle, const ::vfx::RotationOverLifetimeConfig& mod, float /*t*/, float deltaTime)
    {
        // Apply angular velocity (convert degrees to radians)
        particle.rotation += glm::radians(mod.angularVelocity) * deltaTime;
    }
}
