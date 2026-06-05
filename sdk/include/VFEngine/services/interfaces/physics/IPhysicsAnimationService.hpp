#pragma once

#include "../../data/EntityHandle.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include <glm/glm.hpp>

namespace services
{
    class IPhysicsAnimationService
    {
    public:
        virtual ~IPhysicsAnimationService() = default;

        virtual void registerEventHandlers() = 0;

        virtual void activateRagdoll(EntityHandle entity, const glm::vec3& impulse = glm::vec3(0.0f),
                                      int impulseAnimBoneIndex = -1) = 0;
        virtual void deactivateRagdoll(EntityHandle entity) = 0;
        virtual bool isRagdollActive(EntityHandle entity) const = 0;
        virtual types::PhysicsAnimationMode getMode(EntityHandle entity) const = 0;
        virtual bool hasPhysicsAnimation(EntityHandle entity) const = 0;
        virtual void applyRagdollImpulse(EntityHandle entity, const glm::vec3& impulse) = 0;
        virtual void applyRagdollBoneImpulse(EntityHandle entity, int animBoneIndex,
                                              const glm::vec3& impulse) = 0;
    };
}
