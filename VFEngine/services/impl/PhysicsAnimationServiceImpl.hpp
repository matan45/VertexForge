#pragma once

#include "../interfaces/IPhysicsAnimationService.hpp"
#include "../providers/IPhysicsProvider.hpp"

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

    
    };
}
