#pragma once

#include "../data/EntityHandle.hpp"
#include "../interfaces/IPhysicsService.hpp"
#include "types/PhysicsTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <optional>
#include <vector>

namespace services
{
    struct TerrainTileColliderInfo
    {
        int32_t tileX = 0;
        int32_t tileZ = 0;
        const float* heightSamples = nullptr;
        uint32_t sampleCount = 0;
        glm::vec3 worldOrigin{0.0f};
        float vertexSpacing = 1.0f;
        float friction = 0.5f;
        float restitution = 0.0f;
        uint8_t collisionLayer = 0;
    };

    class IPhysicsProvider
    {
    public:
        virtual ~IPhysicsProvider() = default;

        // === Lifecycle ===

        virtual bool init() = 0;
        virtual void cleanUp() = 0;
        virtual bool isInitialized() const = 0;

        // Update physics simulation (called each frame with variable delta)
        // Internally uses fixed timestep accumulator
        virtual void update(float deltaTime) = 0;

        // === Gravity ===

        virtual void setGravity(const glm::vec3& gravity) = 0;
        virtual glm::vec3 getGravity() const = 0;

        // === Rigid Body Operations ===

        virtual void addRigidBody(EntityHandle entity, const RigidBodyData& data,
                                  const ColliderData& collider) = 0;
        virtual void removeRigidBody(EntityHandle entity) = 0;
        virtual bool hasRigidBody(EntityHandle entity) const = 0;
        virtual std::optional<RigidBodyData> getRigidBody(EntityHandle entity) const = 0;

        // === Collider Operations ===

        virtual void addCollider(EntityHandle entity, const ColliderData& data) = 0;
        virtual void removeCollider(EntityHandle entity) = 0;

        // === Force and Impulse ===

        virtual void applyForce(EntityHandle entity, const glm::vec3& force) = 0;
        virtual void applyForceAtPosition(EntityHandle entity, const glm::vec3& force,
                                          const glm::vec3& position) = 0;
        virtual void applyImpulse(EntityHandle entity, const glm::vec3& impulse) = 0;
        virtual void applyTorque(EntityHandle entity, const glm::vec3& torque) = 0;

        // === Velocity Control ===

        virtual void setLinearVelocity(EntityHandle entity, const glm::vec3& velocity) = 0;
        virtual glm::vec3 getLinearVelocity(EntityHandle entity) const = 0;
        virtual void setAngularVelocity(EntityHandle entity, const glm::vec3& velocity) = 0;
        virtual glm::vec3 getAngularVelocity(EntityHandle entity) const = 0;

        // === Transform Access ===
        // Used to sync physics transforms back to entity transforms

        virtual glm::vec3 getPosition(EntityHandle entity) const = 0;
        virtual glm::quat getRotation(EntityHandle entity) const = 0;
        virtual void setPosition(EntityHandle entity, const glm::vec3& position) = 0;
        virtual void setRotation(EntityHandle entity, const glm::quat& rotation) = 0;

        // === Queries ===

        virtual RaycastHit raycast(const glm::vec3& origin, const glm::vec3& direction,
                                   float maxDistance) = 0;
        virtual bool isOverlapping(EntityHandle entityA, EntityHandle entityB) const = 0;

        // === Physics Settings ===

        // Apply physics settings (gravity, timestep, collision matrix, etc.)
        virtual void applySettings(const types::PhysicsSettings& settings) = 0;

        // Get current physics settings
        virtual types::PhysicsSettings getCurrentSettings() const = 0;

        // === Terrain Collider Operations ===

        virtual void addTerrainCollider(EntityHandle entity,
                                         const std::vector<TerrainTileColliderInfo>& tiles) = 0;
        virtual void removeTerrainCollider(EntityHandle entity) = 0;
        virtual void rebuildTerrainTileCollider(EntityHandle entity,
                                                 const TerrainTileColliderInfo& tile) = 0;
        virtual bool hasTerrainCollider(EntityHandle entity) const = 0;
    };
}
