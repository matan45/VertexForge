#include "VFXParticleSystem.hpp"
#include "vfx/VFXShapePlacementMath.hpp"
#include <glm/gtc/constants.hpp>

namespace render::vfx
{
    glm::vec3 VFXParticleSystem::generateSpawnPosition()
    {
        const auto& shape = config.shape;
        bool surfaceOnly = (shape.emitFrom == ::vfx::EmitFrom::Surface);

        // VK-1525: ordered / path-driven placement — walk the shape by emitter age instead of filling
        // randomly, via the shared VFXShapePlacementMath (so the CPU preview matches the GPU sim).
        // review #8: derive the jitter seed the same way the GPU sim does — from (storedSeed, progress,
        // spawn slot) via the shared vfxspOrderedJitterSeed helper — instead of the global rng stream.
        // That makes the scatter a pure function of emitter state, so it reproduces under seek/prewarm
        // and tracks the runtime look, rather than depending on how many particles spawned this session.
        if (shape.ordered)
        {
            float progress = ::vfx::vfxspOrderedProgress(emissionTime, shape.sweepDuration, shape.orderedLoop);
            uint32_t jitterSeed = ::vfx::vfxspOrderedJitterSeed(storedSeed, progress, orderedSpawnSlot++);
            return ::vfx::vfxspOrderedPosition(shape.type, shape.dimensions, progress, shape.orderedJitter, jitterSeed);
        }

        switch (shape.type)
        {
        case ::vfx::ShapeType::Sphere:
            return generateSpherePosition(shape.dimensions.x, surfaceOnly);

        case ::vfx::ShapeType::Cone:
            return generateConePosition(shape.dimensions.x, shape.dimensions.y, shape.dimensions.z, surfaceOnly);

        case ::vfx::ShapeType::Box:
            return generateBoxPosition(glm::vec3(shape.dimensions), surfaceOnly);

        case ::vfx::ShapeType::Line:
            return generateLinePosition(glm::vec3(shape.dimensions));

        case ::vfx::ShapeType::Torus:
            return generateTorusPosition(shape.dimensions.x, shape.dimensions.y, surfaceOnly);

        case ::vfx::ShapeType::Ring:
            return generateRingPosition(shape.dimensions.x, shape.dimensions.y,
                                        shape.dimensions.z, shape.dimensions.w, surfaceOnly);

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

    glm::vec3 VFXParticleSystem::generateLinePosition(const glm::vec3& halfVec)
    {
        // Mirror of GLSL generateLinePosition: centered segment -halfVec -> +halfVec.
        return halfVec * (unitDist(rng) * 2.0f - 1.0f);
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
        float theta = unitDist(rng) * 2.0f * glm::pi<float>();
        float phi = unitDist(rng) * 2.0f * glm::pi<float>();

        float tubeRadius = minorRadius;
        if (!surfaceOnly)
        {
            tubeRadius = minorRadius * std::sqrt(unitDist(rng));
        }

        float ringDist = majorRadius + tubeRadius * std::cos(phi);
        return glm::vec3(
            ringDist * std::cos(theta),
            tubeRadius * std::sin(phi),
            ringDist * std::sin(theta)
        );
    }

    // VK-1525: flat ring / arc / annulus in the XZ plane (mirror of the GLSL generateRingPosition).
    glm::vec3 VFXParticleSystem::generateRingPosition(float radius, float thickness, float arcSpan, float startAngle, bool surfaceOnly)
    {
        float theta = startAngle + unitDist(rng) * arcSpan;

        float r = radius;
        if (!surfaceOnly)
        {
            r = radius + (unitDist(rng) * 2.0f - 1.0f) * thickness;
        }

        return glm::vec3(r * std::cos(theta), 0.0f, r * std::sin(theta));
    }

    glm::vec3 VFXParticleSystem::generateDirectionFromShape(const glm::vec3& position)
    {
        const auto& shape = config.shape;

        if (shape.randomDirection)
        {
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
            glm::vec3 radial = glm::vec3(position.x, 0.0f, position.z);
            float radialLen = glm::length(radial);
            if (radialLen > 0.001f)
            {
                glm::vec3 ringPoint = (radial / radialLen) * shape.dimensions.x;
                glm::vec3 tubeDir = position - ringPoint;
                float tubeLen = glm::length(tubeDir);
                if (tubeLen > 0.001f)
                {
                    return tubeDir / tubeLen;
                }
            }
            return glm::vec3(0.0f, 1.0f, 0.0f);
        }

        case ::vfx::ShapeType::Ring:
        {
            // Radial-outward in the ring plane; upward fallback at the exact center.
            glm::vec3 radial = glm::vec3(position.x, 0.0f, position.z);
            float radialLen = glm::length(radial);
            if (radialLen > 0.001f)
            {
                return radial / radialLen;
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
