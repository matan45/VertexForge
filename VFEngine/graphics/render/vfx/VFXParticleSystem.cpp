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

        // Update time accumulator for noise-based forces (VK-239)
        timeAccumulator += deltaTime;

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
        particle->lifetime = 0.0f;
        particle->maxLifetime = config.lifetime;
        particle->size = config.startSize;
        particle->color = config.startColor;
        particle->rotation = 0.0f;

        // Store initial values for modifier calculations (VK-238)
        particle->initialColor = config.startColor;
        particle->initialSize = config.startSize;
        particle->initialSpeed = config.startSpeed;

        // Generate spawn position based on shape (VK-240)
        particle->position = generateSpawnPosition();

        // Generate direction based on shape config (VK-240)
        glm::vec3 direction = generateDirectionFromShape(particle->position);

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

        // Apply forces before position update (VK-239)
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

    // Force application (VK-239)
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
        // Gravity: constant directional force
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
        // Turbulence: 3D noise-based chaotic movement
        glm::vec3 noisePos = particle.position * force.frequency;
        noisePos += glm::vec3(timeAccumulator * force.scrollSpeed);

        // Generate 3D force from noise (use offset positions for each axis)
        glm::vec3 turbulenceForce;

        // Simple single-octave noise or multi-octave FBM
        if (force.octaves <= 1)
        {
            turbulenceForce.x = glm::simplex(noisePos);
            turbulenceForce.y = glm::simplex(noisePos + glm::vec3(100.0f));
            turbulenceForce.z = glm::simplex(noisePos + glm::vec3(200.0f));
        }
        else
        {
            // Fractal Brownian Motion for richer turbulence
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
        // Vortex: spiral force around an axis
        glm::vec3 toParticle = particle.position - force.center;
        glm::vec3 axis = glm::normalize(force.axis);

        // Project position onto plane perpendicular to axis
        float axisComponent = glm::dot(toParticle, axis);
        glm::vec3 radial = toParticle - axis * axisComponent;
        float dist = glm::length(radial);

        if (dist > 0.001f)
        {
            // Tangential force (perpendicular to both axis and radial)
            glm::vec3 tangent = glm::normalize(glm::cross(axis, radial));
            particle.velocity += tangent * force.strength * deltaTime;

            // Radial pull (inward if negative, outward if positive)
            if (std::abs(force.radialPull) > 0.001f)
            {
                glm::vec3 radialDir = glm::normalize(radial);
                particle.velocity += radialDir * force.radialPull * deltaTime;
            }
        }
    }

    // Shape-based position generation (VK-240)
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

        case ::vfx::ShapeType::Circle:
            return generateCirclePosition(shape.dimensions.x, shape.dimensions.y, surfaceOnly);

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
        // Generate random point on unit sphere using spherical coordinates
        float theta = unitDist(rng) * 2.0f * glm::pi<float>();  // Azimuthal angle [0, 2π]
        float phi = std::acos(1.0f - 2.0f * unitDist(rng));     // Polar angle [0, π] (uniform on sphere)

        glm::vec3 direction;
        direction.x = std::sin(phi) * std::cos(theta);
        direction.y = std::cos(phi);
        direction.z = std::sin(phi) * std::sin(theta);

        float r = radius;
        if (!surfaceOnly)
        {
            // Use cube root for uniform volume distribution
            r = radius * std::cbrt(unitDist(rng));
        }

        return direction * r;
    }

    glm::vec3 VFXParticleSystem::generateConePosition(float baseRadius, float height, float angle, bool surfaceOnly)
    {
        // Cone with apex at origin, opening upward (+Y)
        // Height determines the length, angle determines the spread
        float t = unitDist(rng);  // Position along cone height [0, 1]

        if (!surfaceOnly)
        {
            // Volume distribution - use sqrt for uniform area distribution along height
            t = std::sqrt(unitDist(rng));
        }

        float y = t * height;
        float currentRadius = t * baseRadius * std::tan(angle);

        // Random angle around Y axis
        float theta = unitDist(rng) * 2.0f * glm::pi<float>();

        float r = currentRadius;
        if (!surfaceOnly)
        {
            // Random radius within the cone at this height
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
            // Volume: random point inside box
            return glm::vec3(
                (unitDist(rng) * 2.0f - 1.0f) * halfExtents.x,
                (unitDist(rng) * 2.0f - 1.0f) * halfExtents.y,
                (unitDist(rng) * 2.0f - 1.0f) * halfExtents.z
            );
        }

        // Surface: pick random face, then random point on that face
        // Face areas: 2 * (xy + xz + yz) for full surface
        float areaXY = halfExtents.x * halfExtents.y;
        float areaXZ = halfExtents.x * halfExtents.z;
        float areaYZ = halfExtents.y * halfExtents.z;
        float totalArea = 2.0f * (areaXY + areaXZ + areaYZ);

        float faceSelect = unitDist(rng) * totalArea;
        float u = unitDist(rng) * 2.0f - 1.0f;
        float v = unitDist(rng) * 2.0f - 1.0f;

        if (faceSelect < areaYZ)
        {
            // +X face
            return glm::vec3(halfExtents.x, u * halfExtents.y, v * halfExtents.z);
        }
        faceSelect -= areaYZ;

        if (faceSelect < areaYZ)
        {
            // -X face
            return glm::vec3(-halfExtents.x, u * halfExtents.y, v * halfExtents.z);
        }
        faceSelect -= areaYZ;

        if (faceSelect < areaXZ)
        {
            // +Y face
            return glm::vec3(u * halfExtents.x, halfExtents.y, v * halfExtents.z);
        }
        faceSelect -= areaXZ;

        if (faceSelect < areaXZ)
        {
            // -Y face
            return glm::vec3(u * halfExtents.x, -halfExtents.y, v * halfExtents.z);
        }
        faceSelect -= areaXZ;

        if (faceSelect < areaXY)
        {
            // +Z face
            return glm::vec3(u * halfExtents.x, v * halfExtents.y, halfExtents.z);
        }

        // -Z face
        return glm::vec3(u * halfExtents.x, v * halfExtents.y, -halfExtents.z);
    }

    glm::vec3 VFXParticleSystem::generateCirclePosition(float radius, float arc, bool surfaceOnly)
    {
        // Circle on XZ plane (Y = 0)
        float theta = unitDist(rng) * arc;  // Random angle within arc

        if (surfaceOnly)
        {
            // Edge only
            return glm::vec3(
                radius * std::cos(theta),
                0.0f,
                radius * std::sin(theta)
            );
        }

        // Disk (filled circle) - use sqrt for uniform area distribution
        float r = radius * std::sqrt(unitDist(rng));
        return glm::vec3(
            r * std::cos(theta),
            0.0f,
            r * std::sin(theta)
        );
    }

    glm::vec3 VFXParticleSystem::generateDirectionFromShape(const glm::vec3& position)
    {
        const auto& shape = config.shape;

        // randomDirection = true: use emit direction with random spread
        // randomDirection = false: use shape-based direction (surface normal)
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

        // Generate direction based on shape type (surface normal)
        switch (shape.type)
        {
        case ::vfx::ShapeType::Sphere:
        {
            // Direction is outward from center
            float len = glm::length(position);
            if (len > 0.001f)
            {
                return position / len;
            }
            return glm::vec3(0.0f, 1.0f, 0.0f);
        }

        case ::vfx::ShapeType::Cone:
        {
            // Direction is along the cone surface normal (roughly outward and up)
            float angle = shape.dimensions.z;
            glm::vec3 radial = glm::vec3(position.x, 0.0f, position.z);
            float radialLen = glm::length(radial);

            if (radialLen > 0.001f)
            {
                glm::vec3 outward = radial / radialLen;
                // Blend between outward and up based on cone angle
                return glm::normalize(outward * std::sin(angle) + glm::vec3(0.0f, std::cos(angle), 0.0f));
            }
            return glm::vec3(0.0f, 1.0f, 0.0f);
        }

        case ::vfx::ShapeType::Box:
        {
            // Direction is outward from box face (based on which dimension is at extent)
            const glm::vec3& halfExtents = glm::vec3(shape.dimensions);
            glm::vec3 absPos = glm::abs(position);
            glm::vec3 normalizedPos = absPos / halfExtents;

            // Find which face we're closest to
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

        case ::vfx::ShapeType::Circle:
        {
            // Direction is up (Y+) from XZ plane
            return glm::vec3(0.0f, 1.0f, 0.0f);
        }

        case ::vfx::ShapeType::Point:
        default:
        {
            // Point shape: use emit direction with spread
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
