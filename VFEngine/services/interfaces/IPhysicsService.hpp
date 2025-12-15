#pragma once
#include "../data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace services {

    /**
     * @brief Data for rigid body physics component.
     */
    struct RigidBodyData {
        enum class Type { Static, Dynamic, Kinematic };

        Type type = Type::Dynamic;
        float mass = 1.0f;
        float linearDamping = 0.0f;
        float angularDamping = 0.05f;
        bool useGravity = true;
        glm::vec3 linearVelocity{ 0.0f };
        glm::vec3 angularVelocity{ 0.0f };
    };

    /**
     * @brief Data for collision shape.
     */
    struct ColliderData {
        enum class Shape { Box, Sphere, Capsule, Mesh };

        Shape shape = Shape::Box;
        glm::vec3 size{ 1.0f };     // Box half-extents or sphere/capsule radius
        float height = 1.0f;        // Capsule height
        bool isTrigger = false;
        glm::vec3 offset{ 0.0f };   // Local offset from entity center
    };

    /**
     * @brief Result of a raycast query.
     */
    struct RaycastHit {
        EntityHandle entity;
        glm::vec3 point;
        glm::vec3 normal;
        float distance = 0.0f;
        bool hit = false;
    };

    /**
     * @brief Service interface for physics simulation operations.
     *
     * This service provides high-level APIs for physics simulation
     * using JoltPhysics under the hood.
     *
     * NOTE: This is an interface stub. Implementations will be added
     * when the physics system is fully integrated.
     */
    class IPhysicsService {
    public:
        virtual ~IPhysicsService() = default;

        // === Simulation Control ===

        /**
         * @brief Step the physics simulation.
         * @param deltaTime Time step in seconds
         */
        virtual void stepSimulation(float deltaTime) = 0;

        /**
         * @brief Set global gravity.
         */
        virtual void setGravity(const glm::vec3& gravity) = 0;

        /**
         * @brief Get current global gravity.
         */
        virtual glm::vec3 getGravity() const = 0;

        // === Rigid Body Operations ===

        /**
         * @brief Add a rigid body component to an entity.
         */
        virtual void addRigidBody(EntityHandle entity, const RigidBodyData& data) = 0;

        /**
         * @brief Remove the rigid body component from an entity.
         */
        virtual void removeRigidBody(EntityHandle entity) = 0;

        /**
         * @brief Check if entity has a rigid body.
         */
        virtual bool hasRigidBody(EntityHandle entity) const = 0;

        /**
         * @brief Get rigid body data for an entity.
         */
        virtual std::optional<RigidBodyData> getRigidBody(EntityHandle entity) const = 0;

        // === Collider Operations ===

        /**
         * @brief Add a collider to an entity.
         */
        virtual void addCollider(EntityHandle entity, const ColliderData& data) = 0;

        /**
         * @brief Remove the collider from an entity.
         */
        virtual void removeCollider(EntityHandle entity) = 0;

        // === Force and Impulse ===

        /**
         * @brief Apply a force to a rigid body (accumulated over frame).
         */
        virtual void applyForce(EntityHandle entity, const glm::vec3& force) = 0;

        /**
         * @brief Apply a force at a world position.
         */
        virtual void applyForceAtPosition(EntityHandle entity, const glm::vec3& force,
                                          const glm::vec3& position) = 0;

        /**
         * @brief Apply an instantaneous impulse.
         */
        virtual void applyImpulse(EntityHandle entity, const glm::vec3& impulse) = 0;

        /**
         * @brief Apply torque to a rigid body.
         */
        virtual void applyTorque(EntityHandle entity, const glm::vec3& torque) = 0;

        // === Velocity Control ===

        /**
         * @brief Set linear velocity directly.
         */
        virtual void setLinearVelocity(EntityHandle entity, const glm::vec3& velocity) = 0;

        /**
         * @brief Get current linear velocity.
         */
        virtual glm::vec3 getLinearVelocity(EntityHandle entity) const = 0;

        /**
         * @brief Set angular velocity directly.
         */
        virtual void setAngularVelocity(EntityHandle entity, const glm::vec3& velocity) = 0;

        /**
         * @brief Get current angular velocity.
         */
        virtual glm::vec3 getAngularVelocity(EntityHandle entity) const = 0;

        // === Queries ===

        /**
         * @brief Cast a ray and return the first hit.
         */
        virtual RaycastHit raycast(const glm::vec3& origin, const glm::vec3& direction,
                                   float maxDistance) = 0;

        /**
         * @brief Cast a ray and return all hits.
         */
        virtual std::vector<RaycastHit> raycastAll(const glm::vec3& origin,
                                                    const glm::vec3& direction,
                                                    float maxDistance) = 0;

        /**
         * @brief Check if two entities are overlapping.
         */
        virtual bool isOverlapping(EntityHandle entityA, EntityHandle entityB) const = 0;
    };

}
