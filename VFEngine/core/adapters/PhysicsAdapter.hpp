#pragma once

#include "../../services/providers/IPhysicsProvider.hpp"
#include "../physics/PhysicsWorld.hpp"
#include "../physics/FixedTimestep.hpp"
#include <memory>

namespace core {

    class PhysicsAdapter : public services::IPhysicsProvider {
    public:
        PhysicsAdapter();
        ~PhysicsAdapter() override;

        PhysicsAdapter(const PhysicsAdapter&) = delete;
        PhysicsAdapter& operator=(const PhysicsAdapter&) = delete;

        // === Lifecycle ===

        bool init() override;
        void cleanUp() override;
        bool isInitialized() const override;

        // Update - uses fixed timestep internally
        void update(float deltaTime) override;

        // === Gravity ===

        void setGravity(const glm::vec3& gravity) override;
        glm::vec3 getGravity() const override;

        // === Rigid Body Operations ===

        void addRigidBody(services::EntityHandle entity, const services::RigidBodyData& data,
            const services::ColliderData& collider) override;
        void removeRigidBody(services::EntityHandle entity) override;
        bool hasRigidBody(services::EntityHandle entity) const override;
        std::optional<services::RigidBodyData> getRigidBody(services::EntityHandle entity) const override;

        // === Collider Operations ===

        void addCollider(services::EntityHandle entity, const services::ColliderData& data) override;
        void removeCollider(services::EntityHandle entity) override;

        // === Force and Impulse ===

        void applyForce(services::EntityHandle entity, const glm::vec3& force) override;
        void applyForceAtPosition(services::EntityHandle entity, const glm::vec3& force,
            const glm::vec3& position) override;
        void applyImpulse(services::EntityHandle entity, const glm::vec3& impulse) override;
        void applyTorque(services::EntityHandle entity, const glm::vec3& torque) override;

        // === Velocity Control ===

        void setLinearVelocity(services::EntityHandle entity, const glm::vec3& velocity) override;
        glm::vec3 getLinearVelocity(services::EntityHandle entity) const override;
        void setAngularVelocity(services::EntityHandle entity, const glm::vec3& velocity) override;
        glm::vec3 getAngularVelocity(services::EntityHandle entity) const override;

        // === Transform Access ===

        glm::vec3 getPosition(services::EntityHandle entity) const override;
        glm::quat getRotation(services::EntityHandle entity) const override;
        void setPosition(services::EntityHandle entity, const glm::vec3& position) override;
        void setRotation(services::EntityHandle entity, const glm::quat& rotation) override;

        // === Queries ===

        services::RaycastHit raycast(const glm::vec3& origin, const glm::vec3& direction,
            float maxDistance) override;
        std::vector<services::RaycastHit> raycastAll(const glm::vec3& origin,
            const glm::vec3& direction,
            float maxDistance) override;
        bool isOverlapping(services::EntityHandle entityA, services::EntityHandle entityB) const override;

        // === Interpolation ===

        double getInterpolationAlpha() const override;

        // === Physics Settings ===

        void applySettings(const types::PhysicsSettings& settings) override;
        types::PhysicsSettings getCurrentSettings() const override;

    private:
        std::unique_ptr<physics::PhysicsWorld> physicsWorld;
        std::unique_ptr<physics::FixedTimestep> fixedTimestep;
        types::PhysicsSettings currentSettings;

        // Helper to convert service types to physics types
        physics::RigidBodyCreateInfo toPhysicsBodyInfo(const services::RigidBodyData& data) const;
        physics::ColliderCreateInfo toPhysicsColliderInfo(const services::ColliderData& data) const;
    };

}
