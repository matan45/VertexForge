#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Body/Body.h>
#include <glm/glm.hpp>
#include <functional>
#include <mutex>
#include <vector>

namespace core::physics {

    struct ContactEvent {
        JPH::BodyID bodyA;
        JPH::BodyID bodyB;
        glm::vec3 contactPoint{ 0.0f };
        glm::vec3 normal{ 0.0f };
        float penetrationDepth = 0.0f;
        bool isSensor = false;
    };

    using ContactCallback = std::function<void(const ContactEvent&)>;

    class PhysicsContactListener final : public JPH::ContactListener {
    public:
        PhysicsContactListener() = default;
        ~PhysicsContactListener() override = default;

        // Set callbacks for contact events
        void setOnContactAdded(ContactCallback callback);
        void setOnContactRemoved(ContactCallback callback);

        // Process queued contact events (call from main thread)
        void processContactEvents();

        // JPH::ContactListener overrides
        JPH::ValidateResult OnContactValidate(
            const JPH::Body& inBody1,
            const JPH::Body& inBody2,
            JPH::RVec3Arg inBaseOffset,
            const JPH::CollideShapeResult& inCollisionResult) override;

        void OnContactAdded(
            const JPH::Body& inBody1,
            const JPH::Body& inBody2,
            const JPH::ContactManifold& inManifold,
            JPH::ContactSettings& ioSettings) override;

        void OnContactPersisted(
            const JPH::Body& inBody1,
            const JPH::Body& inBody2,
            const JPH::ContactManifold& inManifold,
            JPH::ContactSettings& ioSettings) override;

        void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;

    private:
        ContactCallback onContactAdded;
        ContactCallback onContactRemoved;

        // Thread-safe event queues (callbacks come from physics threads)
        std::mutex addedMutex;
        std::mutex removedMutex;
        std::vector<ContactEvent> addedEvents;
        std::vector<ContactEvent> removedEvents;
    };

}
