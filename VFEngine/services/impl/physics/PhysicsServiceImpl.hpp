#pragma once

#include "../../interfaces/physics/IPhysicsService.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"

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
            float maxDistance) override;
        bool isOverlapping(EntityHandle entityA, EntityHandle entityB) const override;

    private:
        IPhysicsProvider* physicsProvider;
    };

}
