#pragma once
#include "../interfaces/IControllerService.hpp"
#include <entt/entt.hpp>

namespace components { struct ControllerComponent; }

namespace services {

    class IPhysicsProvider;

    class ControllerServiceImpl : public IControllerService {
    public:
        explicit ControllerServiceImpl(IPhysicsProvider* physicsProvider = nullptr);

        void registerEventHandlers() override;
        void applyControllerMovement(float deltaTime) override;

    private:
        IPhysicsProvider* physicsProvider = nullptr;

        void updateCharacterControllerEntity(entt::entity entity, float deltaTime);
        void updateRigidBodyEntity(entt::entity entity, float deltaTime);
        void deriveLocomotionState(components::ControllerComponent& controller);
        void syncLocomotionToAnimator(entt::entity entity,
                                       const components::ControllerComponent& controller);
    };

}
