#pragma once

#include "../../interfaces/physics/IPhysicsAnimationService.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"

namespace services
{
    class PhysicsAnimationServiceImpl : public IPhysicsAnimationService
    {
    private:
        IPhysicsProvider* physicsProvider;
    public:
        explicit PhysicsAnimationServiceImpl(IPhysicsProvider* physicsProvider);
        ~PhysicsAnimationServiceImpl() override;

        void registerEventHandlers() override;

        void activateRagdoll(EntityHandle entity, const glm::vec3& impulse = glm::vec3(0.0f),
                              int impulseAnimBoneIndex = -1) override;
        void deactivateRagdoll(EntityHandle entity) override;
        bool isRagdollActive(EntityHandle entity) const override;
        types::PhysicsAnimationMode getMode(EntityHandle entity) const override;
        bool hasPhysicsAnimation(EntityHandle entity) const override;
        void applyRagdollImpulse(EntityHandle entity, const glm::vec3& impulse) override;
        void applyRagdollBoneImpulse(EntityHandle entity, int animBoneIndex,
                                      const glm::vec3& impulse) override;

        void setMode(EntityHandle entity, types::PhysicsAnimationMode mode) override;
        void setBoneMotorStrength(EntityHandle entity, const std::string& boneName,
                                   float strength) override;
        void setGlobalMotorStrength(EntityHandle entity, float strength) override;
        void applyHitReaction(EntityHandle entity, const std::string& boneName,
                               const glm::vec3& impulse, float recoverTime = -1.0f) override;
        bool isRagdollSettled(EntityHandle entity) const override;
    };
}
