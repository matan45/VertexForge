#include "VFXParticleSystem.hpp"
#include "threading/JobSystem.hpp"
#include <algorithm>
#include <chrono>
#include <glm/gtc/noise.hpp>

namespace render::vfx
{
    VFXParticleSystem::VFXParticleSystem()
        : rng(static_cast<unsigned int>(std::chrono::steady_clock::now().time_since_epoch().count()))
    {
        particles.resize(VFXConstants::MAX_PARTICLES);
        cachedSegments.reserve(256);
    }

    void VFXParticleSystem::setEmitterConfig(const VFXEmitterConfig& emitterConfig)
    {
        // If render mode or trail points changed, reset ring buffer
        if (config.renderMode != emitterConfig.renderMode ||
            config.maxTrailPoints != emitterConfig.maxTrailPoints)
        {
            ribbonRing.clear();
            ribbonHead = 0;
        }
        config = emitterConfig;
    }

    void VFXParticleSystem::update(float deltaTime)
    {
        if (!playing)
        {
            return;
        }

        timeAccumulator += deltaTime;
        emissionTime += deltaTime;

        // Parallel particle update: each particle is independent (reads config, writes only to itself)
        constexpr size_t PARALLEL_THRESHOLD = 256;
        size_t particleCount = particles.size();

        if (particleCount >= PARALLEL_THRESHOLD)
        {
            uint32_t threadCount = std::max(1u, threading::JobSystem::instance().getThreadCount());
            uint32_t chunkSize = static_cast<uint32_t>((particleCount + threadCount - 1) / threadCount);

            std::vector<std::future<void>> futures;
            futures.reserve(threadCount);

            for (uint32_t t = 0; t < threadCount; ++t)
            {
                uint32_t start = t * chunkSize;
                uint32_t end = std::min(start + chunkSize, static_cast<uint32_t>(particleCount));

                futures.push_back(threading::JobSystem::instance().submit(
                    [this, start, end, deltaTime]()
                    {
                        for (uint32_t i = start; i < end; ++i)
                        {
                            if (particles[i].active)
                            {
                                updateParticle(particles[i], deltaTime);
                            }
                        }
                    }, threading::JobPriority::HIGH
                ));
            }

            for (auto& f : futures)
            {
                f.get();
            }
        }
        else
        {
            for (auto& particle : particles)
            {
                if (particle.active)
                {
                    updateParticle(particle, deltaTime);
                }
            }
        }

        // Spawning must remain sequential (uses rng and shared ribbon state)
        bool canSpawn = config.looping || (emissionTime < config.lifetime);

        if (config.spawnRate > 0.0f && canSpawn)
        {
            spawnAccumulator += deltaTime * config.spawnRate;

            while (spawnAccumulator >= 1.0f)
            {
                spawnParticle();
                spawnAccumulator -= 1.0f;
            }
        }

        if (!config.bursts.empty() && canSpawn)
        {
            std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
            float prevEmissionTime = emissionTime - deltaTime;
            uint32_t burstSpawns = ::vfx::evaluateBurstSpawns(
                config.bursts, prevEmissionTime, emissionTime,
                [this, &dist01]() { return dist01(rng); });

            for (uint32_t i = 0; i < burstSpawns; ++i)
            {
                spawnParticle();
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
        emissionTime = 0.0f;
        timeAccumulator = 0.0f;

        ribbonRing.clear();
        ribbonHead = 0;

        // VK-1451: re-seed deterministically so a reset + replay reproduces the same
        // particle stream (composited sequence preview seek/prewarm). Seed 0 keeps the
        // legacy behavior (RNG continues from wherever it was).
        if (storedSeed != 0)
            rng.seed(storedSeed);
    }

    std::vector<VFXInstanceData> VFXParticleSystem::getInstanceData() const
    {
        std::vector<VFXInstanceData> instances;
        instances.reserve(getActiveParticleCount());

        int totalFrames = config.flipbookRows * config.flipbookColumns;

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

                if (totalFrames > 1)
                {
                    float frameIndex;
                    if (config.flipbookFrameRate > 0.0f)
                    {
                        frameIndex = particle.lifetime * config.flipbookFrameRate;
                    }
                    else
                    {
                        frameIndex = instance.lifetimeRatio * static_cast<float>(totalFrames);
                    }

                    if (config.flipbookRandomStart)
                    {
                        frameIndex += static_cast<float>(particle.spawnSeed % static_cast<uint32_t>(totalFrames));
                    }

                    frameIndex = std::fmod(frameIndex, static_cast<float>(totalFrames));
                    if (frameIndex < 0.0f) frameIndex += static_cast<float>(totalFrames);
                    instance.flipbookFrameIndex = frameIndex;
                }
                else
                {
                    instance.flipbookFrameIndex = 0.0f;
                }

                instance.glowIntensity = particle.glowIntensity;
                instances.push_back(instance);
            }
        }

        return instances;
    }

    const std::vector<VFXRibbonSegmentData>& VFXParticleSystem::getRibbonSegments() const
    {
        cachedSegments.clear();

        if (config.renderMode != VFXRenderMode::Ribbon || config.maxTrailPoints < 2)
            return cachedSegments;

        uint32_t usedPoints = std::min(ribbonHead, config.maxTrailPoints);
        if (usedPoints < 2 || ribbonRing.size() != config.maxTrailPoints)
            return cachedSegments;

        cachedSegments.reserve(usedPoints - 1);

        // Safe from unsigned underflow: usedPoints >= 2 (guarded above) and
        // usedPoints <= ribbonHead (from std::min), so ribbonHead >= 2.
        // Minimum value of (ribbonHead - 2 - i) is ribbonHead - usedPoints >= 0.
        for (uint32_t i = 0; i < usedPoints - 1; i++)
        {
            uint32_t slotA = (ribbonHead - 1 - i) % config.maxTrailPoints;
            uint32_t slotB = (ribbonHead - 2 - i) % config.maxTrailPoints;

            uint32_t pidxA = ribbonRing[slotA];
            uint32_t pidxB = ribbonRing[slotB];

            if (pidxA >= particles.size() || pidxB >= particles.size())
                continue;

            const VFXParticle& pA = particles[pidxA];
            const VFXParticle& pB = particles[pidxB];

            if (!pA.active || !pB.active)
                continue;

            float dist = glm::length(pA.position - pB.position);
            if (dist < config.ribbonMinDistance)
                continue;

            VFXRibbonSegmentData seg{};
            seg.posA = pA.position;
            seg.sizeA = pA.size;
            seg.posB = pB.position;
            seg.sizeB = pB.size;
            seg.colorA = pA.color;
            seg.colorB = pB.color;
            seg.trailT = static_cast<float>(i) / static_cast<float>(usedPoints - 1);
            seg.glowIntensityA = pA.glowIntensity;
            seg.glowIntensityB = pB.glowIntensity;
            seg._pad = 0.0f;

            cachedSegments.push_back(seg);
        }

        return cachedSegments;
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
        particle->lifetime = 0.0f;
        particle->maxLifetime = config.lifetime;
        particle->size = config.startSize;
        particle->color = config.startColor;
        particle->rotation = 0.0f;

        particle->initialColor = config.startColor;
        particle->initialSize = config.startSize;
        particle->initialSpeed = config.startSpeed;

        particle->position = generateSpawnPosition();

        glm::vec3 direction = generateDirectionFromShape(particle->position);

        particle->initialDirection = direction;
        particle->velocity = direction * config.startSpeed;

        particle->spawnSeed = rng();

        if (config.renderMode == VFXRenderMode::Ribbon && config.maxTrailPoints > 0)
        {
            if (ribbonRing.size() != config.maxTrailPoints)
            {
                ribbonRing.resize(config.maxTrailPoints, 0);
            }
            uint32_t particleIndex = static_cast<uint32_t>(particle - particles.data());
            uint32_t slot = ribbonHead % config.maxTrailPoints;
            ribbonRing[slot] = particleIndex;
            ribbonHead++;
        }
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

        if (!config.forces.empty())
        {
            applyForces(particle, deltaTime);
        }

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
        particle.color = mod.gradient.evaluate(t);
    }

    void VFXParticleSystem::applyModifier(VFXParticle& particle, const ::vfx::SizeOverLifetimeConfig& mod, float t, float /*deltaTime*/)
    {
        particle.size = particle.initialSize * mod.curve.evaluate(t);
    }

    void VFXParticleSystem::applyModifier(VFXParticle& particle, const ::vfx::SpeedOverLifetimeConfig& mod, float t, float /*deltaTime*/)
    {
        float multiplier = mod.curve.evaluate(t);
        particle.velocity = particle.initialDirection * particle.initialSpeed * multiplier;
    }

    void VFXParticleSystem::applyModifier(VFXParticle& particle, const ::vfx::RotationOverLifetimeConfig& mod, float t, float deltaTime)
    {
        particle.rotation += glm::radians(mod.curve.evaluate(t)) * deltaTime;
    }

    void VFXParticleSystem::applyModifier(VFXParticle& particle, const ::vfx::GlowOverLifetimeConfig& mod, float t, float /*deltaTime*/)
    {
        particle.glowIntensity = mod.curve.evaluate(t);
    }

    void VFXParticleSystem::applyForces(VFXParticle& particle, float deltaTime)
    {
        for (const auto& force : config.forces.forces)
        {
            std::visit([&](const auto& f) {
                applyForce(particle, f, deltaTime);
            }, force);
        }
    }

    void VFXParticleSystem::applyForce(VFXParticle& particle, const ::vfx::GravityForceConfig& force, float deltaTime)
    {
        glm::vec3 direction = glm::normalize(force.direction);
        particle.velocity += direction * force.strength * deltaTime;
    }

    void VFXParticleSystem::applyForce(VFXParticle& particle, const ::vfx::WindForceConfig& force, float deltaTime)
    {
        glm::vec3 windForce = force.direction * force.strength;

        // Add noise variation if enabled
        if (force.noiseStrength > 0.0f)
        {
            glm::vec3 noisePos = particle.position * force.noiseFrequency + glm::vec3(timeAccumulator);
            float noiseX = glm::simplex(noisePos);
            float noiseY = glm::simplex(noisePos + glm::vec3(100.0f));
            float noiseZ = glm::simplex(noisePos + glm::vec3(200.0f));
            windForce += glm::vec3(noiseX, noiseY, noiseZ) * force.noiseStrength;
        }

        particle.velocity += windForce * deltaTime;
    }

    void VFXParticleSystem::applyForce(VFXParticle& particle, const ::vfx::TurbulenceForceConfig& force, float deltaTime)
    {
        glm::vec3 noisePos = particle.position * force.frequency;
        noisePos += glm::vec3(timeAccumulator * force.scrollSpeed);

        glm::vec3 turbulenceForce;

        if (force.octaves <= 1)
        {
            turbulenceForce.x = glm::simplex(noisePos);
            turbulenceForce.y = glm::simplex(noisePos + glm::vec3(100.0f));
            turbulenceForce.z = glm::simplex(noisePos + glm::vec3(200.0f));
        }
        else
        {
            float amplitude = 1.0f;
            float frequency = 1.0f;
            turbulenceForce = glm::vec3(0.0f);

            for (int i = 0; i < force.octaves; ++i)
            {
                glm::vec3 samplePos = noisePos * frequency;
                turbulenceForce.x += glm::simplex(samplePos) * amplitude;
                turbulenceForce.y += glm::simplex(samplePos + glm::vec3(100.0f)) * amplitude;
                turbulenceForce.z += glm::simplex(samplePos + glm::vec3(200.0f)) * amplitude;

                amplitude *= 0.5f;   // Decay amplitude
                frequency *= 2.0f;   // Increase frequency (lacunarity)
            }
        }

        particle.velocity += turbulenceForce * force.strength * deltaTime;
    }

    void VFXParticleSystem::applyForce(VFXParticle& particle, const ::vfx::VortexForceConfig& force, float deltaTime)
    {
        glm::vec3 toParticle = particle.position - force.center;
        glm::vec3 axis = glm::normalize(force.axis);

        // Project position onto plane perpendicular to axis
        float axisComponent = glm::dot(toParticle, axis);
        glm::vec3 radial = toParticle - axis * axisComponent;
        float dist = glm::length(radial);

        if (dist > 0.001f)
        {
            glm::vec3 tangent = glm::normalize(glm::cross(axis, radial));
            particle.velocity += tangent * force.strength * deltaTime;

            if (std::abs(force.radialPull) > 0.001f)
            {
                glm::vec3 radialDir = glm::normalize(radial);
                particle.velocity += radialDir * force.radialPull * deltaTime;
            }
        }
    }

}
