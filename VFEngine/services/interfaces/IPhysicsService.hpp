#pragma once
#include "../data/EntityHandle.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <vector>
#include <string>

namespace services {

    
    struct RigidBodyData {
        enum class Type { Static, Dynamic, Kinematic };

        Type type = Type::Dynamic;
        float mass = 1.0f;
        float linearDamping = 0.0f;
        float angularDamping = 0.05f;
        glm::vec3 linearVelocity{ 0.0f };
        glm::vec3 angularVelocity{ 0.0f };
    };
    
    struct ColliderData {
        // Shape enum values must match types::ColliderShape for static_assert validation
        enum class Shape { Box = 0, Sphere = 1, Capsule = 2, ConvexMesh = 3, TriangleMesh = 4 };

        Shape shape = Shape::Box;
        glm::vec3 size{ 1.0f };     // Box half-extents or sphere/capsule radius
        float height = 1.0f;        // Capsule height
        bool isTrigger = false;
        uint8_t collisionLayer = 1; // Collision layer (0-15, default 1 = Dynamic)
        glm::vec3 offset{ 0.0f };   // Local offset from entity center
        std::string meshPath;       // Path to mesh for ConvexMesh/TriangleMesh shapes
    };
    
    struct RaycastHit {
        EntityHandle entity;
        glm::vec3 point;
        glm::vec3 normal;
        float distance = 0.0f;
        bool hit = false;
    };
    
    class IPhysicsService {
    public:
        virtual ~IPhysicsService() = default;

        // === Event Handler Registration ===

        virtual void registerEventHandlers() = 0;

        // === Simulation Control ===
        
        virtual void stepSimulation(float deltaTime) = 0;
        
        virtual void setGravity(const glm::vec3& gravity) = 0;
        
        virtual glm::vec3 getGravity() const = 0;

        // === Rigid Body Operations ===

        virtual void addRigidBody(EntityHandle entity, const RigidBodyData& data) = 0;
        
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

        // === Queries ===
        
        virtual RaycastHit raycast(const glm::vec3& origin, const glm::vec3& direction,
                                   float maxDistance) = 0;

        virtual bool isOverlapping(EntityHandle entityA, EntityHandle entityB) const = 0;
    };

}
