#pragma once
#include "../../interfaces/physics/IControllerService.hpp"
#include "CharacterInterpMath.hpp"
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <unordered_map>

namespace components
{
    struct ControllerComponent;
}

namespace services
{
    class IPhysicsProvider;

    class ControllerServiceImpl : public IControllerService
    {
    private:
        IPhysicsProvider* physicsProvider = nullptr;

        // VK-1530: per character-controller entity, the two persistent sim snapshots the
        // render transform is interpolated between (prev/curr shift only when a fixed
        // sub-step runs). Key is entt::entity, whose version bump makes reused slots
        // distinct keys; pruned for destroyed entities in applyControllerMovement.
        std::unordered_map<entt::entity, physics::CharacterInterp> ccInterp;

    public:
        explicit ControllerServiceImpl(IPhysicsProvider* physicsProvider = nullptr);

        void registerEventHandlers() override;
        void applyControllerMovement(float deltaTime) override;

    private:
        void updateCharacterControllerEntity(entt::entity entity);
        void updateRigidBodyEntity(entt::entity entity, float deltaTime);
        void deriveLocomotionState(components::ControllerComponent& controller);
        void syncLocomotionToAnimator(entt::entity entity,
                                      const components::ControllerComponent& controller);
    };
}
