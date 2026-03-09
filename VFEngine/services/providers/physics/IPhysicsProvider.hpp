#pragma once

#include "../../data/EntityHandle.hpp"
#include "../../interfaces/physics/IPhysicsService.hpp"
#include "types/PhysicsTypes.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include "resource/Types.hpp"
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

        virtual bool init() = 0;
        virtual void cleanUp() = 0;
        virtual bool isInitialized() const = 0;
        virtual void update(float deltaTime) = 0;

        virtual void setGravity(const glm::vec3& gravity) = 0;
        virtual glm::vec3 getGravity() const = 0;

        virtual void addRigidBody(EntityHandle entity, const RigidBodyData& data,
                                  const ColliderData& collider) = 0;
        virtual void removeRigidBody(EntityHandle entity) = 0;
        virtual bool hasRigidBody(EntityHandle entity) const = 0;
        virtual std::optional<RigidBodyData> getRigidBody(EntityHandle entity) const = 0;

        virtual void addCollider(EntityHandle entity, const ColliderData& data) = 0;
        virtual void removeCollider(EntityHandle entity) = 0;

        virtual void applyForce(EntityHandle entity, const glm::vec3& force) = 0;
        virtual void applyForceAtPosition(EntityHandle entity, const glm::vec3& force,
                                          const glm::vec3& position) = 0;
        virtual void applyImpulse(EntityHandle entity, const glm::vec3& impulse) = 0;
        virtual void applyTorque(EntityHandle entity, const glm::vec3& torque) = 0;

        virtual void setLinearVelocity(EntityHandle entity, const glm::vec3& velocity) = 0;
        virtual glm::vec3 getLinearVelocity(EntityHandle entity) const = 0;
        virtual void setAngularVelocity(EntityHandle entity, const glm::vec3& velocity) = 0;
        virtual glm::vec3 getAngularVelocity(EntityHandle entity) const = 0;

        virtual glm::vec3 getPosition(EntityHandle entity) const = 0;
        virtual glm::quat getRotation(EntityHandle entity) const = 0;
        virtual void setPosition(EntityHandle entity, const glm::vec3& position) = 0;
        virtual void setRotation(EntityHandle entity, const glm::quat& rotation) = 0;

        virtual RaycastHit raycast(const glm::vec3& origin, const glm::vec3& direction,
                                   float maxDistance, uint16_t layerMask = 0xFFFF) = 0;
        virtual std::vector<RaycastHit> raycastAll(const glm::vec3& origin, const glm::vec3& direction,
                                                   float maxDistance, uint16_t layerMask = 0xFFFF) = 0;
        virtual bool isOverlapping(EntityHandle entityA, EntityHandle entityB) const = 0;

        virtual void applySettings(const types::PhysicsSettings& settings) = 0;
        virtual types::PhysicsSettings getCurrentSettings() const = 0;

        virtual void addTerrainCollider(EntityHandle entity,
                                         const std::vector<TerrainTileColliderInfo>& tiles) = 0;
        virtual void removeTerrainCollider(EntityHandle entity) = 0;
        virtual void rebuildTerrainTileCollider(EntityHandle entity,
                                                 const TerrainTileColliderInfo& tile) = 0;
        virtual bool hasTerrainCollider(EntityHandle entity) const = 0;

        virtual void addTerrainTileCollider(EntityHandle entity, const TerrainTileColliderInfo& tile) = 0;
        virtual void removeTerrainTileCollider(EntityHandle entity, int32_t tileX, int32_t tileZ) = 0;

        // Vegetation colliders — bulk static capsules per tile
        struct VegetationColliderInstance
        {
            glm::vec3 position{0.0f};
            float rotation = 0.0f;      // Y-axis rotation in radians
            float scale = 1.0f;
            float radius = 0.3f;
            float height = 5.0f;
        };

        virtual void addVegetationTileColliders(int32_t tileX, int32_t tileZ,
                                                 const std::vector<VegetationColliderInstance>& instances) = 0;
        virtual void removeVegetationTileColliders(int32_t tileX, int32_t tileZ) = 0;
        virtual void removeAllVegetationColliders() = 0;

        virtual void addWaterSensorBody(EntityHandle entity, const glm::vec3& position,
                                        const glm::vec3& halfExtents) = 0;
        virtual void removeWaterSensorBody(EntityHandle entity) = 0;
        virtual bool hasWaterSensorBody(EntityHandle entity) const = 0;

        // Physics Animation / Ragdoll
        virtual bool createPhysicsAnimation(EntityHandle entity,
                                             const types::PhysicsAnimationConfig& config,
                                             const resource::SkeletonData& skeletonData,
                                             const glm::vec3& entityPosition,
                                             const glm::quat& entityRotation) = 0;
        virtual void destroyPhysicsAnimation(EntityHandle entity) = 0;
        virtual bool hasPhysicsAnimation(EntityHandle entity) const = 0;
        virtual void activateRagdoll(EntityHandle entity) = 0;
        virtual void deactivateRagdoll(EntityHandle entity) = 0;
        virtual bool isRagdollActive(EntityHandle entity) const = 0;
        virtual bool createKinematicBones(EntityHandle entity, const glm::vec3& entityPosition) = 0;
        virtual void destroyKinematicBones(EntityHandle entity) = 0;
        virtual void updateKinematicBones(EntityHandle entity,
                                           const std::vector<glm::mat4>& boneWorldTransforms,
                                           float deltaTime) = 0;
        virtual std::vector<glm::mat4> getRagdollBoneMatrices(
            EntityHandle entity,
            const resource::SkeletonData& skeletonData,
            const std::vector<glm::mat4>& fallbackAnimWorldTransforms) const = 0;
        virtual void transitionToRagdoll(EntityHandle entity,
                                          const std::vector<glm::mat4>& currentBoneWorldTransforms,
                                          const glm::vec3& entityPosition) = 0;
        virtual void transitionToKinematic(EntityHandle entity, const glm::vec3& entityPosition) = 0;
        virtual void applyRagdollImpulse(EntityHandle entity, const glm::vec3& impulse) = 0;
        virtual void applyRagdollBoneImpulse(EntityHandle entity, int animBoneIndex,
                                              const glm::vec3& impulse) = 0;
        virtual void updatePhysicsAnimations(float deltaTime) = 0;

        // Character controller (CharacterVirtual)
        struct CharacterControllerInfo
        {
            types::ColliderShape shape = types::ColliderShape::Capsule;
            glm::vec3 size{1.0f};        // Box half-extents / Sphere: x=radius / Capsule: x=radius, y=height
            float maxSlopeAngle = 45.0f;
            float stepHeight = 0.35f;
            uint8_t collisionLayer = 1;
        };

        struct CharacterUpdateResult
        {
            glm::vec3 position{0.0f};
            glm::vec3 linearVelocity{0.0f};
            bool isGrounded = false;
            glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
            glm::vec3 groundVelocity{0.0f};
        };

        virtual bool addCharacterController(EntityHandle entity, const CharacterControllerInfo& info,
                                             const glm::vec3& position, const glm::quat& rotation) = 0;
        virtual void removeCharacterController(EntityHandle entity) = 0;
        virtual bool hasCharacterController(EntityHandle entity) const = 0;
        virtual CharacterUpdateResult updateCharacterController(EntityHandle entity,
                                                                  const glm::vec3& desiredVelocity,
                                                                  float deltaTime) = 0;
        virtual bool isCharacterGrounded(EntityHandle entity) const = 0;
        virtual glm::vec3 getCharacterPosition(EntityHandle entity) const = 0;
        virtual glm::vec3 getCharacterVelocity(EntityHandle entity) const = 0;
        virtual void setCharacterPosition(EntityHandle entity, const glm::vec3& position) = 0;
    };
}
