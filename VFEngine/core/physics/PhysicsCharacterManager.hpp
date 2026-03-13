#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Body/BodyID.h>
#include "PhysicsLayers.hpp"
#include "types/PhysicsTypes.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <unordered_map>
#include <cstdint>

namespace core::physics
{
    struct PhysicsContext;

    struct CharacterCreateInfo
    {
        types::ColliderShape shape = types::ColliderShape::Capsule;
        glm::vec3 size{1.0f};
        float maxSlopeAngle = 45.0f;
        float stepHeight = 0.35f;
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        uint8_t collisionLayer = 1;
    };

    struct CharacterUpdateResult
    {
        glm::vec3 position{0.0f};
        glm::vec3 linearVelocity{0.0f};
        bool isGrounded = false;
        glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
        glm::vec3 groundVelocity{0.0f};
    };

    class PhysicsCharacterManager
    {
    public:
        void init(PhysicsContext* context);
        void cleanUp();

        bool addCharacter(uint64_t entityId, const CharacterCreateInfo& info);
        void removeCharacter(uint64_t entityId);
        bool hasCharacter(uint64_t entityId) const;
        CharacterUpdateResult updateCharacter(uint64_t entityId, const glm::vec3& desiredVelocity,
                                               float deltaTime, const glm::vec3& gravity);
        bool isCharacterGrounded(uint64_t entityId) const;
        glm::vec3 getCharacterPosition(uint64_t entityId) const;
        glm::vec3 getCharacterLinearVelocity(uint64_t entityId) const;
        void setCharacterPosition(uint64_t entityId, const glm::vec3& position);

    private:
        PhysicsContext* ctx = nullptr;
        std::unordered_map<uint64_t, JPH::Ref<JPH::CharacterVirtual>> entityCharacters;
        std::unordered_map<uint64_t, uint8_t> entityCharacterLayers;
    };
}
