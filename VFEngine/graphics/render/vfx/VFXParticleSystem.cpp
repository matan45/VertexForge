#include "VFXParticleSystem.hpp"
#include <algorithm>
#include <chrono>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/noise.hpp>

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

        timeAccumulator += deltaTime;
        emissionTime += deltaTime;

        for (auto& particle : particles)
        {
            if (particle.active)
            {
                updateParticle(particle, deltaTime);
            }
        }

        // Only spawn new particles if:
        // - looping is enabled, OR
        // - we haven't exceeded the emission duration (one lifetime cycle)
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
    }

    void VFXParticleSystem::reset()
    {
        for (auto& particle : particles)
        {
            particle.active = false;
        }
        spawnAccumulator = 0.0f;
        emissionTime = 0.0f;
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
        particle.color = glm::mix(mod.startColor, mod.endColor, t);
    }

    void VFXParticleSystem::applyModifier(VFXParticle& particle, const ::vfx::SizeOverLifetimeConfig& mod, float t, float /*deltaTime*/)
    {
        float multiplier = glm::mix(mod.startMultiplier, mod.endMultiplier, t);
        particle.size = particle.initialSize * multiplier;
    }

    void VFXParticleSystem::applyModifier(VFXParticle& particle, const ::vfx::SpeedOverLifetimeConfig& mod, float t, float /*deltaTime*/)
    {
        float multiplier = glm::mix(mod.startMultiplier, mod.endMultiplier, t);
        particle.velocity = particle.initialDirection * particle.initialSpeed * multiplier;
    }

    void VFXParticleSystem::applyModifier(VFXParticle& particle, const ::vfx::RotationOverLifetimeConfig& mod, float /*t*/, float deltaTime)
    {
        particle.rotation += glm::radians(mod.angularVelocity) * deltaTime;
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

    glm::vec3 VFXParticleSystem::generateSpawnPosition()
    {
        const auto& shape = config.shape;
        bool surfaceOnly = (shape.emitFrom == ::vfx::EmitFrom::Surface);

        switch (shape.type)
        {
        case ::vfx::ShapeType::Sphere:
            return generateSpherePosition(shape.dimensions.x, surfaceOnly);

        case ::vfx::ShapeType::Cone:
            return generateConePosition(shape.dimensions.x, shape.dimensions.y, shape.dimensions.z, surfaceOnly);

        case ::vfx::ShapeType::Box:
            return generateBoxPosition(glm::vec3(shape.dimensions), surfaceOnly);

        case ::vfx::ShapeType::Torus:
            return generateTorusPosition(shape.dimensions.x, shape.dimensions.y, surfaceOnly);

        case ::vfx::ShapeType::Point:
        default:
            return generatePointPosition();
        }
    }

    glm::vec3 VFXParticleSystem::generatePointPosition()
    {
        return glm::vec3(0.0f);
    }

    glm::vec3 VFXParticleSystem::generateSpherePosition(float radius, bool surfaceOnly)
    {
        float theta = unitDist(rng) * 2.0f * glm::pi<float>();
        float phi = std::acos(1.0f - 2.0f * unitDist(rng));

        glm::vec3 direction;
        direction.x = std::sin(phi) * std::cos(theta);
        direction.y = std::cos(phi);
        direction.z = std::sin(phi) * std::sin(theta);

        float r = radius;
        if (!surfaceOnly)
        {
            r = radius * std::cbrt(unitDist(rng));
        }

        return direction * r;
    }

    glm::vec3 VFXParticleSystem::generateConePosition(float baseRadius, float height, float angle, bool surfaceOnly)
    {
        float t = unitDist(rng);

        if (!surfaceOnly)
        {
            t = std::sqrt(unitDist(rng));
        }

        float y = t * height;
        float currentRadius = t * baseRadius * std::tan(angle);

        float theta = unitDist(rng) * 2.0f * glm::pi<float>();

        float r = currentRadius;
        if (!surfaceOnly)
        {
            r = currentRadius * std::sqrt(unitDist(rng));
        }

        return glm::vec3(
            r * std::cos(theta),
            y,
            r * std::sin(theta)
        );
    }

    glm::vec3 VFXParticleSystem::generateBoxPosition(const glm::vec3& halfExtents, bool surfaceOnly)
    {
        if (!surfaceOnly)
        {
            return glm::vec3(
                (unitDist(rng) * 2.0f - 1.0f) * halfExtents.x,
                (unitDist(rng) * 2.0f - 1.0f) * halfExtents.y,
                (unitDist(rng) * 2.0f - 1.0f) * halfExtents.z
            );
        }

        float areaXY = halfExtents.x * halfExtents.y;
        float areaXZ = halfExtents.x * halfExtents.z;
        float areaYZ = halfExtents.y * halfExtents.z;
        float totalArea = 2.0f * (areaXY + areaXZ + areaYZ);

        float faceSelect = unitDist(rng) * totalArea;
        float u = unitDist(rng) * 2.0f - 1.0f;
        float v = unitDist(rng) * 2.0f - 1.0f;

        if (faceSelect < areaYZ)
            return glm::vec3(halfExtents.x, u * halfExtents.y, v * halfExtents.z);
        faceSelect -= areaYZ;

        if (faceSelect < areaYZ)
            return glm::vec3(-halfExtents.x, u * halfExtents.y, v * halfExtents.z);
        faceSelect -= areaYZ;

        if (faceSelect < areaXZ)
            return glm::vec3(u * halfExtents.x, halfExtents.y, v * halfExtents.z);
        faceSelect -= areaXZ;

        if (faceSelect < areaXZ)
            return glm::vec3(u * halfExtents.x, -halfExtents.y, v * halfExtents.z);
        faceSelect -= areaXZ;

        if (faceSelect < areaXY)
            return glm::vec3(u * halfExtents.x, v * halfExtents.y, halfExtents.z);

        return glm::vec3(u * halfExtents.x, v * halfExtents.y, -halfExtents.z);
    }

    glm::vec3 VFXParticleSystem::generateTorusPosition(float majorRadius, float minorRadius, bool surfaceOnly)
    {
        // theta: angle around the main ring (0 to 2*PI)
        // phi: angle around the tube cross-section (0 to 2*PI)
        float theta = unitDist(rng) * 2.0f * glm::pi<float>();
        float phi = unitDist(rng) * 2.0f * glm::pi<float>();

        float tubeRadius = minorRadius;
        if (!surfaceOnly)
        {
            // For volume emission, sample within the tube
            tubeRadius = minorRadius * std::sqrt(unitDist(rng));
        }

        // Parametric torus equation:
        // x = (R + r*cos(phi)) * cos(theta)
        // y = r * sin(phi)
        // z = (R + r*cos(phi)) * sin(theta)
        float ringDist = majorRadius + tubeRadius * std::cos(phi);
        return glm::vec3(
            ringDist * std::cos(theta),
            tubeRadius * std::sin(phi),
            ringDist * std::sin(theta)
        );
    }

    glm::vec3 VFXParticleSystem::generateDirectionFromShape(const glm::vec3& position)
    {
        const auto& shape = config.shape;

        if (shape.randomDirection)
        {
            // Use emit direction with spread
            glm::vec3 direction = glm::normalize(config.emitDirection);
            float spreadX = randomDist(rng) * 0.2f;
            float spreadZ = randomDist(rng) * 0.2f;
            direction.x += spreadX;
            direction.z += spreadZ;
            return glm::normalize(direction);
        }

        switch (shape.type)
        {
        case ::vfx::ShapeType::Sphere:
        {
            float len = glm::length(position);
            if (len > 0.001f)
            {
                return position / len;
            }
            return glm::vec3(0.0f, 1.0f, 0.0f);
        }

        case ::vfx::ShapeType::Cone:
        {
            float angle = shape.dimensions.z;
            glm::vec3 radial = glm::vec3(position.x, 0.0f, position.z);
            float radialLen = glm::length(radial);

            if (radialLen > 0.001f)
            {
                glm::vec3 outward = radial / radialLen;
                return glm::normalize(outward * std::sin(angle) + glm::vec3(0.0f, std::cos(angle), 0.0f));
            }
            return glm::vec3(0.0f, 1.0f, 0.0f);
        }

        case ::vfx::ShapeType::Box:
        {
            const glm::vec3& halfExtents = glm::vec3(shape.dimensions);
            glm::vec3 absPos = glm::abs(position);
            glm::vec3 normalizedPos = absPos / halfExtents;

            if (normalizedPos.x >= normalizedPos.y && normalizedPos.x >= normalizedPos.z)
            {
                return glm::vec3(position.x > 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
            }
            else if (normalizedPos.y >= normalizedPos.x && normalizedPos.y >= normalizedPos.z)
            {
                return glm::vec3(0.0f, position.y > 0.0f ? 1.0f : -1.0f, 0.0f);
            }
            else
            {
                return glm::vec3(0.0f, 0.0f, position.z > 0.0f ? 1.0f : -1.0f);
            }
        }

        case ::vfx::ShapeType::Torus:
        {
            // Direction points outward from tube center
            // Find the nearest point on the torus ring (center of tube at that angle)
            glm::vec3 radial = glm::vec3(position.x, 0.0f, position.z);
            float radialLen = glm::length(radial);
            if (radialLen > 0.001f)
            {
                glm::vec3 ringPoint = (radial / radialLen) * shape.dimensions.x; // majorRadius
                glm::vec3 tubeDir = position - ringPoint;
                float tubeLen = glm::length(tubeDir);
                if (tubeLen > 0.001f)
                {
                    return tubeDir / tubeLen;
                }
            }
            return glm::vec3(0.0f, 1.0f, 0.0f);
        }

        case ::vfx::ShapeType::Point:
        default:
        {
            glm::vec3 direction = glm::normalize(config.emitDirection);
            float spreadX = randomDist(rng) * 0.2f;
            float spreadZ = randomDist(rng) * 0.2f;
            direction.x += spreadX;
            direction.z += spreadZ;
            return glm::normalize(direction);
        }
        }
    }
}
