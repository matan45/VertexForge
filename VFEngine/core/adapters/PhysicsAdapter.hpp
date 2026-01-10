#pragma once

#include "../../services/providers/IPhysicsProvider.hpp"
#include "../physics/PhysicsWorld.hpp"
#include "../physics/FixedTimestep.hpp"
#include <memory>
#include <mutex>

namespace core
{
    class PhysicsAdapter : public services::IPhysicsProvider
    {
    private:
        std::unique_ptr<physics::PhysicsWorld> physicsWorld;
        std::unique_ptr<physics::FixedTimestep> fixedTimestep;
        types::PhysicsSettings currentSettings;
        mutable std::mutex settingsMutex; // Protects currentSettings access

    public:
        explicit PhysicsAdapter();
        ~PhysicsAdapter() override;

        PhysicsAdapter(const PhysicsAdapter&) = delete;
        PhysicsAdapter& operator=(const PhysicsAdapter&) = delete;

        bool init() override;
        void cleanUp() override;
        bool isInitialized() const override;

        void update(float deltaTime) override;

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

        glm::vec3 getPosition(services::EntityHandle entity) const override;
        glm::quat getRotation(services::EntityHandle entity) const override;
        void setPosition(services::EntityHandle entity, const glm::vec3& position) override;
        void setRotation(services::EntityHandle entity, const glm::quat& rotation) override;

        services::RaycastHit raycast(const glm::vec3& origin, const glm::vec3& direction,
                                     float maxDistance) override;
        bool isOverlapping(services::EntityHandle entityA, services::EntityHandle entityB) const override;

        void applySettings(const types::PhysicsSettings& settings) override;
        types::PhysicsSettings getCurrentSettings() const override;

    private:
        physics::RigidBodyCreateInfo toPhysicsBodyInfo(const services::RigidBodyData& data) const;
        physics::ColliderCreateInfo toPhysicsColliderInfo(const services::ColliderData& data) const;
    };
}
