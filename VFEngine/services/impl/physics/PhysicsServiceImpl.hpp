#pragma once

#include "../../interfaces/physics/IPhysicsService.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"
#include "../../events/EventTypes.hpp"

namespace services {

    class PhysicsServiceImpl : public IPhysicsService {
    public:
        explicit PhysicsServiceImpl(IPhysicsProvider* physicsProvider);
        ~PhysicsServiceImpl() override;

        void registerEventHandlers() override;

        // === Simulation Control ===

        void stepSimulation(float deltaTime) override;
        void setGravity(const glm::vec3& gravity) override;
        glm::vec3 getGravity() const override;

        // === Rigid Body Operations ===

        void addRigidBody(EntityHandle entity, const RigidBodyData& data) override;
        void removeRigidBody(EntityHandle entity) override;
        bool hasRigidBody(EntityHandle entity) const override;
        std::optional<RigidBodyData> getRigidBody(EntityHandle entity) const override;

        // === Collider Operations ===

        void addCollider(EntityHandle entity, const ColliderData& data) override;
        void removeCollider(EntityHandle entity) override;

        bool createVehicle(EntityHandle entity, bool rebuild = false) override;
        bool destroyVehicle(EntityHandle entity) override;
        bool hasVehicle(EntityHandle entity) const override;
        void setVehicleInput(EntityHandle entity, float throttle, float steer, float brake, float handbrake) override;
        std::vector<types::WheelState> getVehicleWheelStates(EntityHandle entity) const override;
        std::optional<types::WheelState> getVehicleWheelState(EntityHandle entity, int wheelIndex) const override;

        // === Force and Impulse ===

        void applyForce(EntityHandle entity, const glm::vec3& force) override;
        void applyForceAtPosition(EntityHandle entity, const glm::vec3& force,
            const glm::vec3& position) override;
        void applyImpulse(EntityHandle entity, const glm::vec3& impulse) override;
        void applyTorque(EntityHandle entity, const glm::vec3& torque) override;

        // === Velocity Control ===

        void setLinearVelocity(EntityHandle entity, const glm::vec3& velocity) override;
        glm::vec3 getLinearVelocity(EntityHandle entity) const override;
        void setAngularVelocity(EntityHandle entity, const glm::vec3& velocity) override;
        glm::vec3 getAngularVelocity(EntityHandle entity) const override;

        // === Queries ===

        RaycastHit raycast(const glm::vec3& origin, const glm::vec3& direction,
            float maxDistance, uint16_t layerMask = 0xFFFF) override;
        std::vector<RaycastHit> raycastAll(const glm::vec3& origin, const glm::vec3& direction,
            float maxDistance, uint16_t layerMask = 0xFFFF) override;
        bool isOverlapping(EntityHandle entityA, EntityHandle entityB) const override;

        std::vector<EntityHandle> overlapSphere(const glm::vec3& center, float radius,
            uint16_t layerMask = 0xFFFF) override;
        std::vector<EntityHandle> overlapBox(const glm::vec3& center, const glm::vec3& halfExtents,
            const glm::quat& rotation, uint16_t layerMask = 0xFFFF) override;
        std::vector<EntityHandle> overlapCapsule(const glm::vec3& center, float halfHeight,
            float radius, const glm::quat& rotation, uint16_t layerMask = 0xFFFF) override;

    private:
        IPhysicsProvider* physicsProvider;
        ::events::SubscriptionToken entityDeletedToken;
    };

}
