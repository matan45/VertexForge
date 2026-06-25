#include "PhysicsRigidBodyManager.hpp"
#include "PhysicsContext.hpp"
#include "PhysicsBodyRegistry.hpp"
#include "PhysicsShapeFactory.hpp"
#include "JoltConversions.hpp"
#include "SpatialQueryHelpers.hpp"
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include "print/Log.hpp"
#include <algorithm>

namespace core::physics
{
    namespace
    {
        template<typename Func>
        float getMotionProp(const JPH::PhysicsSystem* ps, JPH::BodyID bodyId,
                            Func&& getter, float defaultVal)
        {
            if (!ps || bodyId.IsInvalid()) return defaultVal;
            JPH::BodyLockRead lock(ps->GetBodyLockInterface(), bodyId);
            if (lock.Succeeded())
            {
                const auto* mp = lock.GetBody().GetMotionProperties();
                if (mp) return getter(mp);
            }
            return defaultVal;
        }

        JPH::BodyCreationSettings buildRigidBodySettings(
            const JPH::Ref<JPH::Shape>& shape, const RigidBodyCreateInfo& bodyInfo,
            const ColliderCreateInfo& colliderInfo, uint64_t entityId)
        {
            uint8_t clampedLayer;
            if (colliderInfo.collisionLayer < MAX_COLLISION_LAYERS)
            {
                clampedLayer = colliderInfo.collisionLayer;
            }
            else
            {
                clampedLayer = static_cast<uint8_t>(Layers::DYNAMIC);
                vfLogWarning("Entity {}: Invalid collision layer {} (max: {}), defaulting to DYNAMIC ({})",
                              entityId, colliderInfo.collisionLayer, MAX_COLLISION_LAYERS - 1, clampedLayer);
            }

            JPH::BodyCreationSettings settings(
                shape,
                toJoltR(bodyInfo.position),
                toJolt(bodyInfo.rotation),
                PhysicsShapeFactory::getMotionType(bodyInfo.type),
                static_cast<JPH::ObjectLayer>(clampedLayer)
            );

            settings.mLinearVelocity = toJolt(bodyInfo.linearVelocity);
            settings.mAngularVelocity = toJolt(bodyInfo.angularVelocity);
            settings.mFriction = bodyInfo.friction;
            settings.mRestitution = bodyInfo.restitution;
            settings.mLinearDamping = bodyInfo.linearDamping;
            settings.mAngularDamping = bodyInfo.angularDamping;
            settings.mGravityFactor = 1.0f;
            settings.mIsSensor = colliderInfo.isTrigger;
            settings.mUserData = entityId;

            if (bodyInfo.type == BodyType::Dynamic)
            {
                if (bodyInfo.mass > 0.0f)
                {
                    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                    settings.mMassPropertiesOverride.mMass = bodyInfo.mass;
                }
                else
                {
                    vfLogWarning("Entity {}: Invalid mass {} for dynamic body, using shape-calculated mass",
                                  entityId, bodyInfo.mass);
                }
            }

            return settings;
        }

        class LayerMaskFilter final : public JPH::ObjectLayerFilter
        {
        public:
            explicit LayerMaskFilter(uint16_t mask) : mMask(mask) {}

            bool ShouldCollide(JPH::ObjectLayer inLayer) const override
            {
                return layerMaskAllows(mMask, static_cast<uint32_t>(inLayer));
            }

        private:
            uint16_t mMask;
        };
    }

    void PhysicsRigidBodyManager::init(PhysicsContext* context, PhysicsBodyRegistry* registry)
    {
        ctx = context;
        bodyRegistry = registry;
    }

    JPH::BodyID PhysicsRigidBodyManager::addRigidBody(uint64_t entityId, const RigidBodyCreateInfo& bodyInfo,
                                                        const ColliderCreateInfo& colliderInfo)
    {
        if (!ctx || !ctx->physicsSystem) return JPH::BodyID();

        JPH::Ref<JPH::Shape> shape = PhysicsShapeFactory::createShape(colliderInfo);
        if (!shape) return JPH::BodyID();

        if (colliderInfo.offset.x != 0.0f || colliderInfo.offset.y != 0.0f || colliderInfo.offset.z != 0.0f)
        {
            shape = new JPH::RotatedTranslatedShape(
                toJolt(colliderInfo.offset), JPH::Quat::sIdentity(), shape);
        }

        auto settings = buildRigidBodySettings(shape, bodyInfo, colliderInfo, entityId);

        auto& bodyInterface = ctx->getBodyInterface();
        JPH::BodyID bodyId = bodyInterface.CreateAndAddBody(settings,
            bodyInfo.activate ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);

        if (!bodyId.IsInvalid())
        {
            bodyRegistry->registerBody(entityId, bodyId);
        }

        return bodyId;
    }

    void PhysicsRigidBodyManager::removeRigidBody(JPH::BodyID bodyId)
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return;

        bodyRegistry->unregisterBody(bodyId);
        removeAndDestroyBody(ctx->getBodyInterface(), bodyId);
    }

    void PhysicsRigidBodyManager::setBodyEnabled(JPH::BodyID bodyId, bool enabled)
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return;

        auto& bodyInterface = ctx->getBodyInterface();
        if (enabled)
        {
            if (!bodyInterface.IsAdded(bodyId))
                bodyInterface.AddBody(bodyId, JPH::EActivation::Activate);
        }
        else
        {
            if (bodyInterface.IsAdded(bodyId))
                bodyInterface.RemoveBody(bodyId);
        }
    }

    glm::vec3 PhysicsRigidBodyManager::getPosition(JPH::BodyID bodyId) const
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return glm::vec3(0.0f);
        return toGlmR(ctx->getBodyInterface().GetPosition(bodyId));
    }

    glm::quat PhysicsRigidBodyManager::getRotation(JPH::BodyID bodyId) const
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        return toGlm(ctx->getBodyInterface().GetRotation(bodyId));
    }

    void PhysicsRigidBodyManager::setPosition(JPH::BodyID bodyId, const glm::vec3& position)
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return;
        ctx->getBodyInterface().SetPosition(bodyId, toJoltR(position), JPH::EActivation::Activate);
    }

    void PhysicsRigidBodyManager::setRotation(JPH::BodyID bodyId, const glm::quat& rotation)
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return;
        ctx->getBodyInterface().SetRotation(bodyId, toJolt(rotation), JPH::EActivation::Activate);
    }

    void PhysicsRigidBodyManager::setLinearVelocity(JPH::BodyID bodyId, const glm::vec3& velocity)
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return;
        ctx->getBodyInterface().SetLinearVelocity(bodyId, toJolt(velocity));
    }

    glm::vec3 PhysicsRigidBodyManager::getLinearVelocity(JPH::BodyID bodyId) const
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return glm::vec3(0.0f);
        return toGlm(ctx->getBodyInterface().GetLinearVelocity(bodyId));
    }

    void PhysicsRigidBodyManager::setAngularVelocity(JPH::BodyID bodyId, const glm::vec3& velocity)
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return;
        ctx->getBodyInterface().SetAngularVelocity(bodyId, toJolt(velocity));
    }

    glm::vec3 PhysicsRigidBodyManager::getAngularVelocity(JPH::BodyID bodyId) const
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return glm::vec3(0.0f);
        return toGlm(ctx->getBodyInterface().GetAngularVelocity(bodyId));
    }

    BodyType PhysicsRigidBodyManager::getBodyType(JPH::BodyID bodyId) const
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return BodyType::Static;
        auto motionType = ctx->getBodyInterface().GetMotionType(bodyId);
        switch (motionType)
        {
        case JPH::EMotionType::Static: return BodyType::Static;
        case JPH::EMotionType::Kinematic: return BodyType::Kinematic;
        case JPH::EMotionType::Dynamic:
        default: return BodyType::Dynamic;
        }
    }

    float PhysicsRigidBodyManager::getMass(JPH::BodyID bodyId) const
    {
        return getMotionProp(ctx ? ctx->physicsSystem : nullptr, bodyId, [](const JPH::MotionProperties* mp) {
            float inv = mp->GetInverseMass();
            return inv > 0.0f ? 1.0f / inv : 0.0f;
        }, 0.0f);
    }

    float PhysicsRigidBodyManager::getLinearDamping(JPH::BodyID bodyId) const
    {
        return getMotionProp(ctx ? ctx->physicsSystem : nullptr, bodyId,
            [](const auto* mp) { return mp->GetLinearDamping(); }, 0.05f);
    }

    float PhysicsRigidBodyManager::getAngularDamping(JPH::BodyID bodyId) const
    {
        return getMotionProp(ctx ? ctx->physicsSystem : nullptr, bodyId,
            [](const auto* mp) { return mp->GetAngularDamping(); }, 0.05f);
    }

    void PhysicsRigidBodyManager::applyForce(JPH::BodyID bodyId, const glm::vec3& force)
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return;
        ctx->getBodyInterface().AddForce(bodyId, toJolt(force));
    }

    void PhysicsRigidBodyManager::applyForceAtPosition(JPH::BodyID bodyId, const glm::vec3& force,
                                                         const glm::vec3& position)
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return;
        ctx->getBodyInterface().AddForce(bodyId, toJolt(force), toJoltR(position));
    }

    void PhysicsRigidBodyManager::applyImpulse(JPH::BodyID bodyId, const glm::vec3& impulse)
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return;
        ctx->getBodyInterface().AddImpulse(bodyId, toJolt(impulse));
    }

    void PhysicsRigidBodyManager::applyTorque(JPH::BodyID bodyId, const glm::vec3& torque)
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return;
        ctx->getBodyInterface().AddTorque(bodyId, toJolt(torque));
    }

    RaycastResult PhysicsRigidBodyManager::raycast(const glm::vec3& origin, const glm::vec3& direction,
                                                     float maxDistance, uint16_t layerMask) const
    {
        RaycastResult result;
        if (!ctx || !ctx->physicsSystem) return result;

        glm::vec3 normalizedDir = glm::normalize(direction);
        JPH::RRayCast ray(toJoltR(origin), toJolt(normalizedDir * maxDistance));
        JPH::RayCastResult hit;

        LayerMaskFilter layerFilter(layerMask);

        if (ctx->getNarrowPhaseQuery().CastRay(ray, hit, {}, layerFilter))
        {
            result.hit = true;
            result.distance = hit.mFraction * maxDistance;
            result.point = origin + normalizedDir * result.distance;

            result.entityId = bodyRegistry->getEntityForBody(hit.mBodyID);

            JPH::BodyLockRead lock(ctx->getBodyLockInterface(), hit.mBodyID);
            if (lock.Succeeded())
            {
                result.normal = toGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(
                    hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction)));
            }
            else
            {
                result.normal = -normalizedDir;
            }
        }

        return result;
    }

    std::vector<RaycastResult> PhysicsRigidBodyManager::raycastAll(const glm::vec3& origin, const glm::vec3& direction,
                                                                     float maxDistance, uint16_t layerMask) const
    {
        std::vector<RaycastResult> results;
        if (!ctx || !ctx->physicsSystem) return results;

        glm::vec3 normalizedDir = glm::normalize(direction);
        JPH::RRayCast ray(toJoltR(origin), toJolt(normalizedDir * maxDistance));

        JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
        JPH::RayCastSettings settings;
        LayerMaskFilter layerFilter(layerMask);

        ctx->getNarrowPhaseQuery().CastRay(ray, settings, collector, {}, layerFilter);

        if (!collector.HadHit())
            return results;

        collector.Sort();

        results.reserve(collector.mHits.size());
        for (const auto& hit : collector.mHits)
        {
            RaycastResult result;
            result.hit = true;
            result.distance = hit.mFraction * maxDistance;
            result.point = origin + normalizedDir * result.distance;

            result.entityId = bodyRegistry->getEntityForBody(hit.mBodyID);

            JPH::BodyLockRead lock(ctx->getBodyLockInterface(), hit.mBodyID);
            if (lock.Succeeded())
            {
                result.normal = toGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(
                    hit.mSubShapeID2, ray.GetPointOnRay(hit.mFraction)));
            }
            else
            {
                result.normal = -normalizedDir;
            }

            results.push_back(result);
        }

        return results;
    }

    std::vector<uint64_t> PhysicsRigidBodyManager::overlapSphere(const glm::vec3& center, float radius,
                                                                  uint16_t layerMask) const
    {
        std::vector<uint64_t> entityIds;
        if (!ctx || !ctx->physicsSystem) return entityIds;
        if (radius <= 0.0f) return entityIds;  // SphereShape asserts radius > 0

        JPH::SphereShape shape(radius);
        return collectOverlap(&shape, JPH::RMat44::sTranslation(toJoltR(center)), layerMask);
    }

    std::vector<uint64_t> PhysicsRigidBodyManager::overlapBox(const glm::vec3& center,
                                                               const glm::vec3& halfExtents,
                                                               const glm::quat& rotation,
                                                               uint16_t layerMask) const
    {
        std::vector<uint64_t> entityIds;
        if (!ctx || !ctx->physicsSystem) return entityIds;
        // BoxShape asserts every half-extent >= 0; the convex radius is internally
        // clamped to the smallest half-extent so a tiny box is still valid.
        glm::vec3 he = glm::max(halfExtents, glm::vec3(0.0f));
        if (he.x <= 0.0f && he.y <= 0.0f && he.z <= 0.0f) return entityIds;

        JPH::BoxShape shape(toJolt(he));
        return collectOverlap(&shape,
            JPH::RMat44::sRotationTranslation(toJolt(rotation), toJoltR(center)), layerMask);
    }

    std::vector<uint64_t> PhysicsRigidBodyManager::overlapCapsule(const glm::vec3& center,
                                                                   float halfHeight, float radius,
                                                                   const glm::quat& rotation,
                                                                   uint16_t layerMask) const
    {
        std::vector<uint64_t> entityIds;
        if (!ctx || !ctx->physicsSystem) return entityIds;
        // CapsuleShape asserts halfHeight > 0 and radius > 0 (a 0 half-height would
        // otherwise be a sphere; require a real capsule here).
        if (halfHeight <= 0.0f || radius <= 0.0f) return entityIds;

        JPH::CapsuleShape shape(halfHeight, radius);
        return collectOverlap(&shape,
            JPH::RMat44::sRotationTranslation(toJolt(rotation), toJoltR(center)), layerMask);
    }

    std::vector<uint64_t> PhysicsRigidBodyManager::collectOverlap(const JPH::Shape* shape,
                                                                   const JPH::RMat44& centerOfMassTransform,
                                                                   uint16_t layerMask) const
    {
        std::vector<uint64_t> entityIds;

        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        JPH::CollideShapeSettings settings;
        LayerMaskFilter layerFilter(layerMask);

        ctx->getNarrowPhaseQuery().CollideShape(
            shape, JPH::Vec3::sReplicate(1.0f), centerOfMassTransform, settings,
            JPH::RVec3::sZero(), collector, {}, layerFilter);

        if (!collector.HadHit()) return entityIds;

        entityIds.reserve(collector.mHits.size());
        for (const auto& hit : collector.mHits)
        {
            entityIds.push_back(bodyRegistry->getEntityForBody(hit.mBodyID2));
        }

        // A compound/mesh body can report several sub-shape hits — collapse to one id per entity.
        dedupeEntityIds(entityIds);
        return entityIds;
    }

    bool PhysicsRigidBodyManager::areBodiesInContact(JPH::BodyID bodyA, JPH::BodyID bodyB) const
    {
        if (!ctx || !ctx->physicsSystem || bodyA.IsInvalid() || bodyB.IsInvalid()) return false;
        return ctx->physicsSystem->WereBodiesInContact(bodyA, bodyB);
    }

    bool PhysicsRigidBodyManager::isBodyActive(JPH::BodyID bodyId) const
    {
        if (!ctx || !ctx->physicsSystem || bodyId.IsInvalid()) return false;
        return ctx->getBodyInterface().IsActive(bodyId);
    }
}
