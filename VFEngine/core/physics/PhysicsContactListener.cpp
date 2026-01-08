#include "PhysicsContactListener.hpp"
#include <Jolt/Physics/Collision/Shape/SubShapeIDPair.h>
#include <Jolt/Physics/Collision/CollideShape.h>

namespace core::physics {

    void PhysicsContactListener::setOnContactAdded(ContactCallback callback) {
        onContactAdded = std::move(callback);
    }

    void PhysicsContactListener::setOnContactRemoved(ContactCallback callback) {
        onContactRemoved = std::move(callback);
    }

    void PhysicsContactListener::processContactEvents() {
        // Process added events
        {
            std::lock_guard lock(addedMutex);
            if (onContactAdded) {
                for (const auto& event : addedEvents) {
                    onContactAdded(event);
                }
            }
            addedEvents.clear();
        }

        // Process removed events
        {
            std::lock_guard lock(removedMutex);
            if (onContactRemoved) {
                for (const auto& event : removedEvents) {
                    onContactRemoved(event);
                }
            }
            removedEvents.clear();
        }
    }

    JPH::ValidateResult PhysicsContactListener::OnContactValidate(
        const JPH::Body& inBody1,
        const JPH::Body& inBody2,
        JPH::RVec3Arg inBaseOffset,
        const JPH::CollideShapeResult& inCollisionResult) {
        // Accept all contacts by default
        // Can be extended later for custom filtering
        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void PhysicsContactListener::OnContactAdded(
        const JPH::Body& inBody1,
        const JPH::Body& inBody2,
        const JPH::ContactManifold& inManifold,
        JPH::ContactSettings& ioSettings) {

        ContactEvent event;
        event.bodyA = inBody1.GetID();
        event.bodyB = inBody2.GetID();
        event.penetrationDepth = inManifold.mPenetrationDepth;
        event.isSensor = ioSettings.mIsSensor;

        // Get contact point (first point if available)
        if (!inManifold.mRelativeContactPointsOn1.empty()) {
            JPH::RVec3 worldPoint = inManifold.GetWorldSpaceContactPointOn1(0);
            event.contactPoint = glm::vec3(
                static_cast<float>(worldPoint.GetX()),
                static_cast<float>(worldPoint.GetY()),
                static_cast<float>(worldPoint.GetZ())
            );
        }

        // Get normal
        event.normal = glm::vec3(
            inManifold.mWorldSpaceNormal.GetX(),
            inManifold.mWorldSpaceNormal.GetY(),
            inManifold.mWorldSpaceNormal.GetZ()
        );

        std::lock_guard lock(addedMutex);
        addedEvents.push_back(event);
    }

    void PhysicsContactListener::OnContactPersisted(
        const JPH::Body& inBody1,
        const JPH::Body& inBody2,
        const JPH::ContactManifold& inManifold,
        JPH::ContactSettings& ioSettings) {
        // Can be extended to track ongoing collisions if needed
        // For now, we only care about add/remove events
    }

    void PhysicsContactListener::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) {
        ContactEvent event;
        event.bodyA = inSubShapePair.GetBody1ID();
        event.bodyB = inSubShapePair.GetBody2ID();

        std::lock_guard lock(removedMutex);
        removedEvents.push_back(event);
    }

}
