#pragma once

#include "../../services/providers/physics/IPhysicsProvider.hpp"
#include "../../services/events/EventTypes.hpp"
#include "../../physics/PhysicsWorld.hpp"
#include "../../physics/FixedTimestep.hpp"
#include "../../physics/RagdollSettingsBuilder.hpp"
#include <memory>
#include <mutex>
#include <future>
#include <unordered_map>
#include <unordered_set>

namespace core
{
    class PhysicsAdapter : public services::IPhysicsProvider
    {
    private:
        std::unique_ptr<physics::PhysicsWorld> physicsWorld;
        std::unique_ptr<physics::FixedTimestep> fixedTimestep;
        types::PhysicsSettings currentSettings;
        mutable std::mutex settingsMutex;
        std::function<void(float)> postStepCallback;

    public:
        explicit PhysicsAdapter();
        ~PhysicsAdapter() override;

        PhysicsAdapter(const PhysicsAdapter&) = delete;
        PhysicsAdapter& operator=(const PhysicsAdapter&) = delete;

        bool init() override;
        void cleanUp() override;
        bool isInitialized() const override;

        void update(float deltaTime) override;
        void setPostStepCallback(std::function<void(float)> callback) override;

        void kickPhysicsStep(float deltaTime) override;
        void syncPhysicsStep() override;
        float getInterpolationAlpha() const override;
        services::PhysicsTransformSnapshot getInterpolatedTransform(services::EntityHandle entity) const override;

        void setGravity(const glm::vec3& gravity) override;
        glm::vec3 getGravity() const override;

        void addRigidBody(services::EntityHandle entity, const services::RigidBodyData& data,
                          const services::ColliderData& collider) override;
        void removeRigidBody(services::EntityHandle entity) override;
        bool hasRigidBody(services::EntityHandle entity) const override;
        std::optional<services::RigidBodyData> getRigidBody(services::EntityHandle entity) const override;

        void addCollider(services::EntityHandle entity, const services::ColliderData& data) override;
        void removeCollider(services::EntityHandle entity) override;

        void applyForce(services::EntityHandle entity, const glm::vec3& force) override;
        void applyForceAtPosition(services::EntityHandle entity, const glm::vec3& force,
                                  const glm::vec3& position) override;
        void applyImpulse(services::EntityHandle entity, const glm::vec3& impulse) override;
        void applyTorque(services::EntityHandle entity, const glm::vec3& torque) override;

        void setLinearVelocity(services::EntityHandle entity, const glm::vec3& velocity) override;
        glm::vec3 getLinearVelocity(services::EntityHandle entity) const override;
        void setAngularVelocity(services::EntityHandle entity, const glm::vec3& velocity) override;
        glm::vec3 getAngularVelocity(services::EntityHandle entity) const override;

        bool isBodySleeping(services::EntityHandle entity) const override;

        glm::vec3 getPosition(services::EntityHandle entity) const override;
        glm::quat getRotation(services::EntityHandle entity) const override;
        void setPosition(services::EntityHandle entity, const glm::vec3& position) override;
        void setRotation(services::EntityHandle entity, const glm::quat& rotation) override;

        services::RaycastHit raycast(const glm::vec3& origin, const glm::vec3& direction,
                                     float maxDistance, uint16_t layerMask = 0xFFFF) override;
        std::vector<services::RaycastHit> raycastAll(const glm::vec3& origin, const glm::vec3& direction,
                                                     float maxDistance, uint16_t layerMask = 0xFFFF) override;
        bool isOverlapping(services::EntityHandle entityA, services::EntityHandle entityB) const override;

        void applySettings(const types::PhysicsSettings& settings) override;
        types::PhysicsSettings getCurrentSettings() const override;

        void addTerrainCollider(services::EntityHandle entity,
                                 const std::vector<services::TerrainTileColliderInfo>& tiles) override;
        void removeTerrainCollider(services::EntityHandle entity) override;
        void rebuildTerrainTileCollider(services::EntityHandle entity,
                                         const services::TerrainTileColliderInfo& tile) override;
        bool hasTerrainCollider(services::EntityHandle entity) const override;

        void addTerrainTileCollider(services::EntityHandle entity,
                                     const services::TerrainTileColliderInfo& tile) override;
        void removeTerrainTileCollider(services::EntityHandle entity,
                                        int32_t tileX, int32_t tileZ) override;

        void addCaveTileCollider(services::EntityHandle entity,
                                  const services::CaveTileColliderInfo& cave) override;
        void removeCaveTileCollider(services::EntityHandle entity,
                                     int32_t tileX, int32_t tileZ) override;
        void rebuildCaveTileCollider(services::EntityHandle entity,
                                      const services::CaveTileColliderInfo& cave) override;

        void addVegetationTileColliders(int32_t tileX, int32_t tileZ,
                                         const std::vector<VegetationColliderInstance>& instances) override;
        void removeVegetationTileColliders(int32_t tileX, int32_t tileZ) override;
        void removeAllVegetationColliders() override;

        void addWaterSensorBody(services::EntityHandle entity, const glm::vec3& position,
                                const glm::vec3& halfExtents) override;
        void removeWaterSensorBody(services::EntityHandle entity) override;
        bool hasWaterSensorBody(services::EntityHandle entity) const override;

        // Physics Animation / Ragdoll
        bool createPhysicsAnimation(services::EntityHandle entity,
                                     const types::PhysicsAnimationConfig& config,
                                     const resource::SkeletonData& skeletonData,
                                     const glm::vec3& entityPosition,
                                     const glm::quat& entityRotation) override;
        void destroyPhysicsAnimation(services::EntityHandle entity) override;
        bool hasPhysicsAnimation(services::EntityHandle entity) const override;
        void activateRagdoll(services::EntityHandle entity) override;
        void deactivateRagdoll(services::EntityHandle entity) override;
        bool isRagdollActive(services::EntityHandle entity) const override;
        bool createKinematicBones(services::EntityHandle entity, const glm::vec3& entityPosition) override;
        void destroyKinematicBones(services::EntityHandle entity) override;
        void updateKinematicBones(services::EntityHandle entity,
                                   const std::vector<glm::mat4>& boneWorldTransforms,
                                   float deltaTime) override;
        std::vector<glm::mat4> getRagdollBoneMatrices(
            services::EntityHandle entity,
            const resource::SkeletonData& skeletonData,
            const std::vector<glm::mat4>& fallbackAnimWorldTransforms) const override;
        void transitionToRagdoll(services::EntityHandle entity,
                                  const std::vector<glm::mat4>& currentBoneWorldTransforms,
                                  const glm::vec3& entityPosition) override;
        void transitionToKinematic(services::EntityHandle entity, const glm::vec3& entityPosition) override;
        void applyRagdollImpulse(services::EntityHandle entity, const glm::vec3& impulse) override;
        void applyRagdollBoneImpulse(services::EntityHandle entity, int animBoneIndex,
                                      const glm::vec3& impulse) override;
        void updatePhysicsAnimations(float deltaTime) override;

        // Character controller
        bool addCharacterController(services::EntityHandle entity, const CharacterControllerInfo& info,
                                     const glm::vec3& position, const glm::quat& rotation) override;
        void removeCharacterController(services::EntityHandle entity) override;
        bool hasCharacterController(services::EntityHandle entity) const override;
        CharacterUpdateResult updateCharacterController(services::EntityHandle entity,
                                                          const glm::vec3& desiredVelocity,
                                                          float deltaTime) override;
        bool isCharacterGrounded(services::EntityHandle entity) const override;
        glm::vec3 getCharacterPosition(services::EntityHandle entity) const override;
        glm::vec3 getCharacterVelocity(services::EntityHandle entity) const override;
        void setCharacterPosition(services::EntityHandle entity, const glm::vec3& position) override;

    private:
        events::SubscriptionToken assetReleaseToken;
        std::unordered_set<uint64_t> waterSensorEntities;

        std::future<physics::FixedTimestepResult> asyncStepFuture;
        float lastStepAlpha = 0.0f;
        bool asyncStepInFlight = false;

        struct PhysicsAnimationState
        {
            physics::RagdollBuildResult buildResult;
            resource::SkeletonData skeletonData;
        };
        std::unordered_map<uint64_t, PhysicsAnimationState> physicsAnimationEntities;

        void onContactAdded(const physics::ContactEvent& event);
        void onContactRemoved(const physics::ContactEvent& event);
    };
}
