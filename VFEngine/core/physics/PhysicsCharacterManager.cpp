#include "PhysicsCharacterManager.hpp"
#include "PhysicsContext.hpp"
#include "JoltConversions.hpp"
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include "print/Log.hpp"

namespace core::physics
{
    void PhysicsCharacterManager::init(PhysicsContext* context)
    {
        ctx = context;
    }

    void PhysicsCharacterManager::cleanUp()
    {
        entityCharacters.clear();
        entityCharacterLayers.clear();
    }

    bool PhysicsCharacterManager::addCharacter(uint64_t entityId, const CharacterCreateInfo& info)
    {
        if (!ctx || !ctx->physicsSystem) return false;
        if (entityCharacters.count(entityId)) return false;

        JPH::RefConst<JPH::Shape> characterShape;
        switch (info.shape)
        {
        case types::ColliderShape::Sphere:
            characterShape = new JPH::SphereShape(info.size.x);
            break;
        case types::ColliderShape::Box:
            characterShape = new JPH::BoxShape(JPH::Vec3(info.size.x, info.size.y, info.size.z));
            break;
        case types::ColliderShape::Capsule:
        default:
        {
            float radius = info.size.x;
            float totalHeight = info.size.y;
            float cylinderHalfHeight = (totalHeight * 0.5f) - radius;
            if (cylinderHalfHeight < 0.01f) cylinderHalfHeight = 0.01f;
            characterShape = new JPH::CapsuleShape(cylinderHalfHeight, radius);
            break;
        }
        }

        JPH::CharacterVirtualSettings settings;
        settings.mShape = characterShape;
        settings.mMaxSlopeAngle = JPH::DegreesToRadians(info.maxSlopeAngle);
        settings.mMaxStrength = 100.0f;
        settings.mMass = 70.0f;
        settings.mPredictiveContactDistance = 0.1f;
        settings.mPenetrationRecoverySpeed = 1.0f;
        settings.mEnhancedInternalEdgeRemoval = true;

        auto character = new JPH::CharacterVirtual(
            &settings,
            toJoltR(info.position),
            toJolt(info.rotation),
            entityId,
            ctx->physicsSystem
        );

        entityCharacters[entityId] = character;
        entityCharacterLayers[entityId] = info.collisionLayer;

        vfLogInfo("Character controller created for entity {} at ({:.1f}, {:.1f}, {:.1f})",
                   entityId, info.position.x, info.position.y, info.position.z);
        return true;
    }

    void PhysicsCharacterManager::removeCharacter(uint64_t entityId)
    {
        entityCharacters.erase(entityId);
        entityCharacterLayers.erase(entityId);
    }

    bool PhysicsCharacterManager::hasCharacter(uint64_t entityId) const
    {
        return entityCharacters.count(entityId) > 0;
    }

    CharacterUpdateResult PhysicsCharacterManager::updateCharacter(uint64_t entityId,
                                                                     const glm::vec3& desiredVelocity,
                                                                     float deltaTime,
                                                                     const glm::vec3& gravity)
    {
        CharacterUpdateResult result;
        auto it = entityCharacters.find(entityId);
        if (it == entityCharacters.end() || !ctx || !ctx->physicsSystem) return result;

        auto* character = it->second.GetPtr();

        character->SetLinearVelocity(toJolt(desiredVelocity));

        JPH::CharacterVirtual::ExtendedUpdateSettings updateSettings;
        updateSettings.mStickToFloorStepDown = JPH::Vec3(0.0f, -0.5f, 0.0f);
        updateSettings.mWalkStairsStepUp = JPH::Vec3(0.0f, 0.4f, 0.0f);

        auto layerIt = entityCharacterLayers.find(entityId);
        JPH::ObjectLayer charLayer = static_cast<JPH::ObjectLayer>(
            layerIt != entityCharacterLayers.end() ? layerIt->second : Layers::DYNAMIC);
        JPH::DefaultBroadPhaseLayerFilter broadPhaseFilter(*ctx->broadPhaseFilter, charLayer);
        JPH::DefaultObjectLayerFilter objectLayerFilter(*ctx->objectLayerFilter, charLayer);
        JPH::BodyFilter bodyFilter;
        JPH::ShapeFilter shapeFilter;

        character->ExtendedUpdate(
            deltaTime,
            toJolt(gravity),
            updateSettings,
            broadPhaseFilter,
            objectLayerFilter,
            bodyFilter,
            shapeFilter,
            *ctx->tempAllocator
        );

        result.position = toGlmR(character->GetPosition());
        result.linearVelocity = toGlm(character->GetLinearVelocity());
        result.groundNormal = toGlm(character->GetGroundNormal());
        result.groundVelocity = toGlm(character->GetGroundVelocity());

        auto groundState = character->GetGroundState();
        result.isGrounded = (groundState == JPH::CharacterBase::EGroundState::OnGround);

        return result;
    }

    bool PhysicsCharacterManager::isCharacterGrounded(uint64_t entityId) const
    {
        auto it = entityCharacters.find(entityId);
        if (it == entityCharacters.end()) return false;
        return it->second->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    }

    glm::vec3 PhysicsCharacterManager::getCharacterPosition(uint64_t entityId) const
    {
        auto it = entityCharacters.find(entityId);
        if (it == entityCharacters.end()) return glm::vec3(0.0f);
        return toGlmR(it->second->GetPosition());
    }

    glm::vec3 PhysicsCharacterManager::getCharacterLinearVelocity(uint64_t entityId) const
    {
        auto it = entityCharacters.find(entityId);
        if (it == entityCharacters.end()) return glm::vec3(0.0f);
        return toGlm(it->second->GetLinearVelocity());
    }

    void PhysicsCharacterManager::setCharacterPosition(uint64_t entityId, const glm::vec3& position)
    {
        auto it = entityCharacters.find(entityId);
        if (it == entityCharacters.end()) return;
        it->second->SetPosition(toJoltR(position));
    }
}
