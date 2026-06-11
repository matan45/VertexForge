#pragma once

#include "../../data/EntityHandle.hpp"
#include "types/PhysicsAnimationTypes.hpp"
#include <glm/glm.hpp>
#include <string>

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

        // Powered ragdoll
        virtual void setMode(EntityHandle entity, types::PhysicsAnimationMode mode) = 0;
        virtual void setBoneMotorStrength(EntityHandle entity, const std::string& boneName,
                                           float strength) = 0;
        virtual void setGlobalMotorStrength(EntityHandle entity, float strength) = 0;
        virtual void applyHitReaction(EntityHandle entity, const std::string& boneName,
                                       const glm::vec3& impulse, float recoverTime = -1.0f) = 0;
        virtual bool isRagdollSettled(EntityHandle entity) const = 0;
    };
}
