#include "ControllerServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/physics/PhysicsEvents.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "../../events/animation/AnimatorEvents.hpp"
#include "../../providers/physics/IPhysicsProvider.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/ControllerComponents.hpp"
#include "components/CoreComponents.hpp"
#include "components/NavmeshComponents.hpp"
#include "components/PhysicsComponents.hpp"
#include "components/MediaComponents.hpp"
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

namespace services
{
    ControllerServiceImpl::ControllerServiceImpl(IPhysicsProvider* physicsProvider)
        : physicsProvider(physicsProvider)
    {
    }

    void ControllerServiceImpl::applyControllerMovement(float deltaTime)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& dispatcher = ::events::EventDispatcher::instance();
        auto view = registry.view<components::ControllerComponent, components::TransformComponent>();

        for (auto entity : view)
        {
            auto& controller = view.get<components::ControllerComponent>(entity);
            auto& transform = view.get<components::TransformComponent>(entity);
            auto handle = internal::toHandle(entity);

            if (controller.hasMoveToTarget)
            {
                float dist = glm::length(controller.moveToDestination - transform.position);
                if (dist < controller.arrivalDistance)
                {
                    controller.hasMoveToTarget = false;
                    controller.moveToDestinationDirty = false;
                    controller.moveInput = glm::vec3(0.0f);

                    if (registry.all_of<components::NavmeshAgentComponent>(entity))
                    {
                        ::events::navmesh::StopAgentCommand stopCmd;
                        stopCmd.entity = handle;
                        dispatcher.execute(stopCmd);
                    }
                    continue;
                }

                if (registry.all_of<components::NavmeshAgentComponent>(entity))
                {
                    auto& agent = registry.get<components::NavmeshAgentComponent>(entity);
                    float targetSpeed = controller.moveSpeed;
                    if (controller.wantsSprint)
                    {
                        targetSpeed *= controller.sprintMultiplier;
                    }
                    agent.maxSpeed = targetSpeed;

                    if (controller.moveToDestinationDirty)
                    {
                        ::events::navmesh::SetAgentDestinationCommand destCmd;
                        destCmd.entity = handle;
                        destCmd.target = controller.moveToDestination;
                        dispatcher.execute(destCmd);
                        controller.moveToDestinationDirty = false;
                    }
                    continue;
                }

                glm::vec3 dir = glm::normalize(controller.moveToDestination - transform.position);
                controller.moveInput = dir;
            }

            if (controller.characterControllerActive && physicsProvider)
            {
                updateCharacterControllerEntity(entity, deltaTime);
            }
            else
            {
                updateRigidBodyEntity(entity, deltaTime);
            }

            deriveLocomotionState(controller);
            syncLocomotionToAnimator(entity, controller);

            controller.wantsJump = false;
            if (!controller.hasMoveToTarget)
            {
                controller.moveInput = glm::vec3(0.0f);
            }
            controller.wantsSprint = false;
        }
    }

    void ControllerServiceImpl::updateCharacterControllerEntity(entt::entity entity, float deltaTime)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& controller = registry.get<components::ControllerComponent>(entity);
        auto& transform = registry.get<components::TransformComponent>(entity);
        auto handle = internal::toHandle(entity);

        float targetSpeed = controller.moveSpeed;
        if (controller.wantsSprint)
        {
            targetSpeed *= controller.sprintMultiplier;
        }

        glm::vec3 desiredHorizontal{0.0f};
        if (glm::length2(controller.moveInput) > 0.001f)
        {
            glm::vec3 normalizedInput = glm::normalize(controller.moveInput);
            desiredHorizontal = normalizedInput * targetSpeed;
        }

        glm::vec3 currentHorizontal{controller.currentVelocity.x, 0.0f, controller.currentVelocity.z};
        glm::vec3 velocityDiff = desiredHorizontal - currentHorizontal;
        float diffLen = glm::length(velocityDiff);

        if (diffLen > 0.001f)
        {
            float rate = glm::length2(desiredHorizontal) >= glm::length2(currentHorizontal)
                ? controller.acceleration
                : controller.deceleration;

            if (!controller.isGrounded)
            {
                rate *= controller.airControlFactor;
            }

            float maxDelta = rate * deltaTime;
            if (diffLen <= maxDelta)
            {
                currentHorizontal = desiredHorizontal;
            }
            else
            {
                currentHorizontal += (velocityDiff / diffLen) * maxDelta;
            }
        }

        float verticalVel = controller.currentVelocity.y;
        auto gravity = physicsProvider->getGravity();
        verticalVel += gravity.y * deltaTime;

        if (controller.wantsJump && controller.jumpForce > 0.0f && controller.isGrounded)
        {
            verticalVel = controller.jumpForce;
        }

        glm::vec3 fullVelocity{currentHorizontal.x, verticalVel, currentHorizontal.z};

        auto result = physicsProvider->updateCharacterController(handle, fullVelocity, deltaTime);

        transform.position = result.position;
        transform.isDirty = true;

        controller.isGrounded = result.isGrounded;
        controller.currentVelocity = result.linearVelocity;
        controller.verticalVelocity = result.linearVelocity.y;
        controller.currentSpeed = glm::length(glm::vec2(result.linearVelocity.x, result.linearVelocity.z));
    }

    void ControllerServiceImpl::updateRigidBodyEntity(entt::entity entity, float deltaTime)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        auto& controller = registry.get<components::ControllerComponent>(entity);
        auto& transform = registry.get<components::TransformComponent>(entity);
        auto handle = internal::toHandle(entity);
        auto& dispatcher = ::events::EventDispatcher::instance();

        if (glm::length(controller.moveInput) > 0.001f)
        {
            float speed = controller.moveSpeed;
            if (controller.wantsSprint)
            {
                speed *= controller.sprintMultiplier;
            }

            glm::vec3 velocity = controller.moveInput * speed;

            if (registry.all_of<components::RigidBodyComponent>(entity))
            {
                ::events::physics::SetLinearVelocityCommand velCmd;
                velCmd.entity = handle;
                velCmd.velocity = glm::vec3(velocity.x, 0.0f, velocity.z);
                dispatcher.execute(velCmd);
            }
            else
            {
                transform.position += velocity * deltaTime;
                transform.isDirty = true;
            }

            controller.currentVelocity = velocity;
            controller.currentSpeed = glm::length(glm::vec2(velocity.x, velocity.z));
        }
        else
        {
            controller.currentVelocity = glm::vec3(0.0f);
            controller.currentSpeed = 0.0f;
        }

        if (controller.wantsJump && controller.jumpForce > 0.0f && controller.isGrounded)
        {
            if (registry.all_of<components::RigidBodyComponent>(entity))
            {
                ::events::physics::ApplyImpulseCommand impulseCmd;
                impulseCmd.entity = handle;
                impulseCmd.impulse = glm::vec3(0.0f, controller.jumpForce, 0.0f);
                dispatcher.execute(impulseCmd);
                controller.isGrounded = false;
            }
        }
    }

    void ControllerServiceImpl::deriveLocomotionState(components::ControllerComponent& controller)
    {
        const auto& config = controller.locomotionConfig;

        const std::string* newState = nullptr;

        if (!controller.isGrounded)
        {
            newState = controller.verticalVelocity > 0.1f
                ? &config.jumpState
                : &config.fallState;
        }
        else if (controller.currentSpeed < 0.1f)
        {
            newState = &config.idleState;
        }
        else if (controller.currentSpeed < controller.walkSpeedThreshold)
        {
            newState = &config.walkState;
        }
        else
        {
            newState = &config.runState;
        }

        if (newState && !newState->empty())
        {
            controller.locomotionState = *newState;
        }
    }

    void ControllerServiceImpl::syncLocomotionToAnimator(entt::entity entity,
                                                          const components::ControllerComponent& controller)
    {
        if (!controller.locomotionConfig.syncToAnimator) return;

        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::AnimatorComponent>(entity)) return;

        const auto& animComp = registry.get<components::AnimatorComponent>(entity);
        if (!animComp.isInitialized) return;

        auto& dispatcher = ::events::EventDispatcher::instance();
        auto handle = internal::toHandle(entity);

        bool hasDirection = glm::length2(controller.moveInput) > 0.001f;

        for (const auto& mapping : controller.locomotionConfig.paramMappings)
        {
            if (mapping.paramName.empty()) continue;

            switch (mapping.source)
            {
            case components::LocomotionParamSource::Speed:
            {
                ::services::events::animator::SetEntityAnimatorFloatCommand cmd;
                cmd.entity = handle;
                cmd.parameterName = mapping.paramName;
                cmd.value = controller.currentSpeed;
                dispatcher.execute(cmd);
                break;
            }
            case components::LocomotionParamSource::Grounded:
            {
                ::services::events::animator::SetEntityAnimatorBoolCommand cmd;
                cmd.entity = handle;
                cmd.parameterName = mapping.paramName;
                cmd.value = controller.isGrounded;
                dispatcher.execute(cmd);
                break;
            }
            case components::LocomotionParamSource::VerticalVelocity:
            {
                ::services::events::animator::SetEntityAnimatorFloatCommand cmd;
                cmd.entity = handle;
                cmd.parameterName = mapping.paramName;
                cmd.value = controller.verticalVelocity;
                dispatcher.execute(cmd);
                break;
            }
            case components::LocomotionParamSource::DirectionX:
            {
                if (!hasDirection) break;
                ::services::events::animator::SetEntityAnimatorFloatCommand cmd;
                cmd.entity = handle;
                cmd.parameterName = mapping.paramName;
                cmd.value = controller.moveInput.x;
                dispatcher.execute(cmd);
                break;
            }
            case components::LocomotionParamSource::DirectionY:
            {
                if (!hasDirection) break;
                ::services::events::animator::SetEntityAnimatorFloatCommand cmd;
                cmd.entity = handle;
                cmd.parameterName = mapping.paramName;
                cmd.value = controller.moveInput.z;
                dispatcher.execute(cmd);
                break;
            }
            }
        }
    }
}
