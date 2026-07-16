#pragma once
#include "../../interfaces/physics/IControllerService.hpp"
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

        // VK-1530: previous rendered sim position per character-controller entity,
        // used to interpolate the render transform between fixed steps (key is
        // entt::entity, whose version bump makes reused slots distinct keys).
        std::unordered_map<entt::entity, glm::vec3> ccLastSimPos;

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
