#include "VFXParticleSystem.hpp"
#include "threading/JobSystem.hpp"
#include "vfx/VFXVariance.hpp"
#include "vfx/VFXCurlNoise.hpp"
#include "vfx/VFXKillVolume.hpp"
#include "vfx/VFXSpeedRemap.hpp"
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
                // VK-1476: carried for the mesh preview's orientation modes so the
                // preview matches runtime. age = elapsed seconds (matches GPUParticle.lifetime).
                instance.velocity = particle.velocity;
                instance.spawnSeed = particle.spawnSeed;
                instance.age = particle.lifetime;

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

            // VK-1474: fold the over-trail width curve + tail gradient into the endpoints
            // (per-vertex t: head=0 -> tail=1). No-op when absent -> byte-identical legacy geometry.
            if (config.hasRibbonWidthCurve || config.hasRibbonTailGradient)
            {
                const float denom = static_cast<float>(usedPoints - 1);
                const float tA = static_cast<float>(i) / denom;
                const float tB = static_cast<float>(i + 1) / denom;
                if (config.hasRibbonWidthCurve)
                {
                    seg.sizeA *= config.ribbonWidthCurve.evaluate(tA);
                    seg.sizeB *= config.ribbonWidthCurve.evaluate(tB);
                }
                if (config.hasRibbonTailGradient)
                {
                    seg.colorA *= config.ribbonTailGradient.evaluate(tA);
                    seg.colorB *= config.ribbonTailGradient.evaluate(tB);
                }
            }

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

        const uint32_t spawnSeed = particle->spawnSeed;
        const float sizeMult = std::max(0.0f, 1.0f + config.sizeVariance *
            ::vfx::vfxVarianceSigned(spawnSeed, ::vfx::VarianceStream::Size));
        const float lifetimeMult = std::max(0.01f, 1.0f + config.lifetimeVariance *
            ::vfx::vfxVarianceSigned(spawnSeed, ::vfx::VarianceStream::Lifetime));
        const float speedMult = std::max(0.0f, 1.0f + config.speedVariance *
            ::vfx::vfxVarianceSigned(spawnSeed, ::vfx::VarianceStream::Speed));
        particle->colorValueMult = 1.0f + config.colorValueVariance *
            ::vfx::vfxVarianceSigned(spawnSeed, ::vfx::VarianceStream::ColorValue);
        particle->alphaMult = 1.0f + config.alphaVariance *
            ::vfx::vfxVarianceSigned(spawnSeed, ::vfx::VarianceStream::Alpha);

        particle->maxLifetime = config.lifetime * lifetimeMult;
        particle->size = config.startSize * sizeMult;
        particle->initialSize = particle->size;
        particle->initialSpeed = config.startSpeed * speedMult;
        particle->rotation = config.rotationVariance *
            ::vfx::vfxVarianceSigned(spawnSeed, ::vfx::VarianceStream::Rotation);
        particle->angularVelocity = config.angularVelocityVariance *
            ::vfx::vfxVarianceSigned(spawnSeed, ::vfx::VarianceStream::AngularVelocity);
        particle->velocity = direction * particle->initialSpeed;
        particle->color = config.startColor;
        particle->color.r *= particle->colorValueMult;
        particle->color.g *= particle->colorValueMult;
        particle->color.b *= particle->colorValueMult;
        particle->color.a *= particle->alphaMult;
        particle->initialColor = particle->color;

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
                particle.color.a = config.startColor.a * particle.alphaMult * (1.0f - fadeProgress);
            }
        }

        if (!config.forces.empty())
        {
            applyForces(particle, deltaTime);
        }

        particle.position += particle.velocity * deltaTime;
        particle.rotation += particle.angularVelocity * deltaTime;

        // Kill-at-center (mirror GPU): kill any particle that reached the center of an
        // attractor flagged killAtCenter, checked after the position update. Kill volume
        // is also checked here; CPU preview has no emitter transform, so Local==World.
        for (const auto& force : config.forces.forces)
        {
            if (auto* killVolume = std::get_if<::vfx::KillVolumeForceConfig>(&force))
            {
                if (::vfx::killedByVolume(particle.position, *killVolume))
                {
                    particle.active = false;
                    return;
                }
            }
            else if (auto* attractor = std::get_if<::vfx::PointAttractorForceConfig>(&force))
            {
                if (attractor->killAtCenter)
                {
                    float killRadius = std::max(0.05f, attractor->radius * 0.05f);
                    if (glm::length(attractor->position - particle.position) < killRadius)
                    {
                        particle.active = false;
                        return;
                    }
                }
            }
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

    void VFXParticleSystem::applyModifiers(VFXParticle& particle, float lifetimeRatio, float deltaTime)
    {
        // Pass 1: over-lifetime modifiers (sample by age, overwrite color/size/etc).
        for (const auto& modifier : config.modifiers.modifiers)
        {
            std::visit([&](const auto& mod) {
                using T = std::decay_t<decltype(mod)>;
                if constexpr (!std::is_same_v<T, ::vfx::SizeBySpeedConfig> &&
                              !std::is_same_v<T, ::vfx::ColorBySpeedConfig>)
                {
                    applyModifier(particle, mod, lifetimeRatio, deltaTime);
                }
            }, modifier);
        }

        // Pass 2: by-speed modifiers, multiplied on top (order-independent of graph position).
        applyBySpeedModifiers(particle);
    }

    void VFXParticleSystem::applyBySpeedModifiers(VFXParticle& particle)
    {
        bool hasSizeOverLifetime = false;
        bool hasColorOverLifetime = false;
        bool hasBySpeed = false;
        for (const auto& modifier : config.modifiers.modifiers)
        {
            if (std::holds_alternative<::vfx::SizeOverLifetimeConfig>(modifier)) hasSizeOverLifetime = true;
            else if (std::holds_alternative<::vfx::ColorOverLifetimeConfig>(modifier)) hasColorOverLifetime = true;
            else if (std::holds_alternative<::vfx::SizeBySpeedConfig>(modifier) ||
                     std::holds_alternative<::vfx::ColorBySpeedConfig>(modifier)) hasBySpeed = true;
        }
        if (!hasBySpeed)
            return;

        const float speed = glm::length(particle.velocity);
        for (const auto& modifier : config.modifiers.modifiers)
        {
            if (const auto* m = std::get_if<::vfx::SizeBySpeedConfig>(&modifier))
            {
                const float t = ::vfx::normalizedSpeed01(speed, m->speedMin, m->speedMax);
                const float base = hasSizeOverLifetime ? particle.size : particle.initialSize;
                particle.size = base * m->curve.evaluate(t);
            }
            else if (const auto* m = std::get_if<::vfx::ColorBySpeedConfig>(&modifier))
            {
                const float t = ::vfx::normalizedSpeed01(speed, m->speedMin, m->speedMax);
                const glm::vec4 base = hasColorOverLifetime ? particle.color : particle.initialColor;
                particle.color = base * m->gradient.evaluate(t);
            }
        }
    }

    void VFXParticleSystem::applyModifier(VFXParticle& particle, const ::vfx::ColorOverLifetimeConfig& mod, float t, float /*deltaTime*/)
    {
        particle.color = mod.gradient.evaluate(t);
        particle.color.r *= particle.colorValueMult;
        particle.color.g *= particle.colorValueMult;
        particle.color.b *= particle.colorValueMult;
        particle.color.a *= particle.alphaMult;
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
        // Apply all non-drag (position-based) forces first, then drag last. This mirrors
        // the GPU, where forces accumulate into totalForce, velocity integrates once, and
        // drag then divides the fully-integrated velocity.
        for (const auto& force : config.forces.forces)
        {
            if (std::holds_alternative<::vfx::DragForceConfig>(force))
                continue;
            std::visit([&](const auto& f) {
                applyForce(particle, f, deltaTime);
            }, force);
        }

        for (const auto& force : config.forces.forces)
        {
            if (auto* drag = std::get_if<::vfx::DragForceConfig>(&force))
                applyForce(particle, *drag, deltaTime);
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

    void VFXParticleSystem::applyForce(VFXParticle& particle, const ::vfx::DragForceConfig& force, float deltaTime)
    {
        // Multiplicative (semi-implicit) form: never reverses velocity, even when k*dt > 1.
        // Must match vfx_particle_sim.glsl exactly.
        float k = force.linearCoeff + force.quadraticCoeff * glm::length(particle.velocity);
        particle.velocity /= (1.0f + std::max(k, 0.0f) * deltaTime);
    }

    void VFXParticleSystem::applyForce(VFXParticle& particle, const ::vfx::PointAttractorForceConfig& force, float deltaTime)
    {
        // Pull toward a world-space point with radius/falloff. Matches vfx_particle_sim.glsl.
        glm::vec3 toCenter = force.position - particle.position;
        float dist = glm::length(toCenter);
        if (dist > 1e-4f && dist < force.radius)
        {
            float t = glm::clamp(1.0f - dist / force.radius, 0.0f, 1.0f);
            float falloff = std::pow(t, force.falloff);
            particle.velocity += (toCenter / dist) * force.strength * falloff * deltaTime;
        }
    }

    void VFXParticleSystem::applyForce(VFXParticle& particle, const ::vfx::CurlNoiseForceConfig& force, float deltaTime)
    {
        // Divergence-free curl noise. Single tested kernel shared with the divergence
        // doctest; the GPU mirror lives in vfx_particle_sim.glsl. Like Turbulence, the
        // CPU uses glm::simplex + timeAccumulator while the GPU uses its own simplex +
        // frameNumber*0.016 (same basis, matches to float rounding).
        particle.velocity += ::vfx::evalCurlNoise(force.strength, force.frequency, force.scrollSpeed,
                                                  force.octaves, particle.position, timeAccumulator) *
                             deltaTime;
    }

    void VFXParticleSystem::applyForce(VFXParticle& /*particle*/, const ::vfx::KillVolumeForceConfig& /*force*/, float /*deltaTime*/)
    {
        // Kill Volume is a post-integration predicate, not an acceleration force.
    }

}
