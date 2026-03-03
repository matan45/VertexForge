#include "ControllerServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/ControllerEvents.hpp"
#include "../events/PhysicsEvents.hpp"
#include "../events/NavmeshEvents.hpp"
#include "../events/AnimatorEvents.hpp"
#include "../providers/IPhysicsProvider.hpp"
#include "../data/EntityConversion.hpp"
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
    void ControllerServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // Movement input commands
        dispatcher.registerCommandHandler<::events::controller::SetMoveInputCommand>(
            [](const ::events::controller::SetMoveInputCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                registry.get<components::ControllerComponent>(entity).moveInput = cmd.moveInput;
            });

        dispatcher.registerCommandHandler<::events::controller::SetJumpCommand>(
            [](const ::events::controller::SetJumpCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                registry.get<components::ControllerComponent>(entity).wantsJump = cmd.wantsJump;
            });

        dispatcher.registerCommandHandler<::events::controller::SetSprintCommand>(
            [](const ::events::controller::SetSprintCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                registry.get<components::ControllerComponent>(entity).wantsSprint = cmd.wantsSprint;
            });

        dispatcher.registerCommandHandler<::events::controller::MoveToCommand>(
            [](const ::events::controller::MoveToCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return false;
                }
                auto& controller = registry.get<components::ControllerComponent>(entity);
                controller.hasMoveToTarget = true;
                controller.moveToDestinationDirty = true;
                controller.moveToDestination = cmd.destination;
                return true;
            });

        dispatcher.registerCommandHandler<::events::controller::StopMovementCommand>(
            [](const ::events::controller::StopMovementCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                auto& controller = registry.get<components::ControllerComponent>(entity);
                controller.moveInput = glm::vec3(0.0f);
                controller.hasMoveToTarget = false;
                controller.moveToDestinationDirty = false;
                controller.wantsJump = false;
                controller.wantsSprint = false;

                // Stop navmesh agent if present
                if (registry.all_of<components::NavmeshAgentComponent>(entity))
                {
                    ::events::navmesh::StopAgentCommand stopCmd;
                    stopCmd.entity = cmd.entity;
                    ::events::EventDispatcher::instance().execute(stopCmd);
                }
            });

        dispatcher.registerCommandHandler<::events::controller::SetMoveSpeedCommand>(
            [](const ::events::controller::SetMoveSpeedCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                registry.get<components::ControllerComponent>(entity).moveSpeed = cmd.moveSpeed;
            });

        // Queries
        dispatcher.registerQueryHandler<::events::controller::HasReachedDestinationQuery>(
            [](const ::events::controller::HasReachedDestinationQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return true;
                }
                auto& controller = registry.get<components::ControllerComponent>(entity);
                if (!controller.hasMoveToTarget)
                {
                    return true;
                }
                if (!registry.all_of<components::TransformComponent>(entity))
                {
                    return true;
                }
                auto& transform = registry.get<components::TransformComponent>(entity);
                float dist = glm::length(controller.moveToDestination - transform.position);
                return dist < controller.arrivalDistance;
            });

        dispatcher.registerQueryHandler<::events::controller::GetDistanceToQuery>(
            [](const ::events::controller::GetDistanceToQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::TransformComponent>(entity))
                {
                    return 0.0f;
                }
                auto& transform = registry.get<components::TransformComponent>(entity);
                return glm::length(query.target - transform.position);
            });

        dispatcher.registerQueryHandler<::events::controller::GetMoveSpeedQuery>(
            [](const ::events::controller::GetMoveSpeedQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return 0.0f;
                }
                return registry.get<components::ControllerComponent>(entity).moveSpeed;
            });

        dispatcher.registerCommandHandler<::events::controller::SetJumpForceCommand>(
            [](const ::events::controller::SetJumpForceCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                registry.get<components::ControllerComponent>(entity).jumpForce = cmd.jumpForce;
            });

        dispatcher.registerCommandHandler<::events::controller::SetSprintMultiplierCommand>(
            [](const ::events::controller::SetSprintMultiplierCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                registry.get<components::ControllerComponent>(entity).sprintMultiplier = cmd.sprintMultiplier;
            });

        dispatcher.registerQueryHandler<::events::controller::GetJumpForceQuery>(
            [](const ::events::controller::GetJumpForceQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return 0.0f;
                }
                return registry.get<components::ControllerComponent>(entity).jumpForce;
            });

        dispatcher.registerQueryHandler<::events::controller::GetSprintMultiplierQuery>(
            [](const ::events::controller::GetSprintMultiplierQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return 0.0f;
                }
                return registry.get<components::ControllerComponent>(entity).sprintMultiplier;
            });

        dispatcher.registerCommandHandler<::events::controller::SetArrivalDistanceCommand>(
            [](const ::events::controller::SetArrivalDistanceCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                registry.get<components::ControllerComponent>(entity).arrivalDistance = cmd.arrivalDistance;
            });

        dispatcher.registerQueryHandler<::events::controller::GetArrivalDistanceQuery>(
            [](const ::events::controller::GetArrivalDistanceQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return 0.5f;
                }
                return registry.get<components::ControllerComponent>(entity).arrivalDistance;
            });

        dispatcher.registerCommandHandler<::events::controller::SetGroundedCommand>(
            [](const ::events::controller::SetGroundedCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                registry.get<components::ControllerComponent>(entity).isGrounded = cmd.isGrounded;
            });

        dispatcher.registerQueryHandler<::events::controller::IsGroundedQuery>(
            [](const ::events::controller::IsGroundedQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return false;
                }
                return registry.get<components::ControllerComponent>(entity).isGrounded;
            });

        // Locomotion settings commands
        dispatcher.registerCommandHandler<::events::controller::SetAccelerationCommand>(
            [](const ::events::controller::SetAccelerationCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                registry.get<components::ControllerComponent>(entity).acceleration = cmd.acceleration;
            });

        dispatcher.registerCommandHandler<::events::controller::SetDecelerationCommand>(
            [](const ::events::controller::SetDecelerationCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                registry.get<components::ControllerComponent>(entity).deceleration = cmd.deceleration;
            });

        dispatcher.registerCommandHandler<::events::controller::SetRotationSpeedCommand>(
            [](const ::events::controller::SetRotationSpeedCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                registry.get<components::ControllerComponent>(entity).rotationSpeed = cmd.rotationSpeed;
            });

        dispatcher.registerCommandHandler<::events::controller::SetAirControlFactorCommand>(
            [](const ::events::controller::SetAirControlFactorCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                registry.get<components::ControllerComponent>(entity).airControlFactor = cmd.airControlFactor;
            });

        dispatcher.registerCommandHandler<::events::controller::SetWalkSpeedThresholdCommand>(
            [](const ::events::controller::SetWalkSpeedThresholdCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return;
                }
                registry.get<components::ControllerComponent>(entity).walkSpeedThreshold = cmd.walkSpeedThreshold;
            });

        // Locomotion state queries
        dispatcher.registerQueryHandler<::events::controller::GetLocomotionStateQuery>(
            [](const ::events::controller::GetLocomotionStateQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return 0;
                }
                return static_cast<int>(registry.get<components::ControllerComponent>(entity).locomotionState);
            });

        dispatcher.registerQueryHandler<::events::controller::GetCurrentSpeedQuery>(
            [](const ::events::controller::GetCurrentSpeedQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return 0.0f;
                }
                return registry.get<components::ControllerComponent>(entity).currentSpeed;
            });

        dispatcher.registerQueryHandler<::events::controller::GetVerticalVelocityQuery>(
            [](const ::events::controller::GetVerticalVelocityQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return 0.0f;
                }
                return registry.get<components::ControllerComponent>(entity).verticalVelocity;
            });

        dispatcher.registerQueryHandler<::events::controller::GetAccelerationQuery>(
            [](const ::events::controller::GetAccelerationQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return 0.0f;
                }
                return registry.get<components::ControllerComponent>(entity).acceleration;
            });

        dispatcher.registerQueryHandler<::events::controller::GetDecelerationQuery>(
            [](const ::events::controller::GetDecelerationQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return 0.0f;
                }
                return registry.get<components::ControllerComponent>(entity).deceleration;
            });

        dispatcher.registerQueryHandler<::events::controller::GetRotationSpeedQuery>(
            [](const ::events::controller::GetRotationSpeedQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return 0.0f;
                }
                return registry.get<components::ControllerComponent>(entity).rotationSpeed;
            });

        dispatcher.registerQueryHandler<::events::controller::GetAirControlFactorQuery>(
            [](const ::events::controller::GetAirControlFactorQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return 0.0f;
                }
                return registry.get<components::ControllerComponent>(entity).airControlFactor;
            });

        dispatcher.registerQueryHandler<::events::controller::GetWalkSpeedThresholdQuery>(
            [](const ::events::controller::GetWalkSpeedThresholdQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                {
                    return 0.0f;
                }
                return registry.get<components::ControllerComponent>(entity).walkSpeedThreshold;
            });
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

            // Handle MoveTo (navmesh pathfinding or direct)
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

                // Delegate to navmesh if available (only dispatch when destination changed)
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

                // Otherwise compute direction for direct movement
                glm::vec3 dir = glm::normalize(controller.moveToDestination - transform.position);
                controller.moveInput = dir;
            }

            // Route to character controller or rigid body path
            if (controller.joltCharacter != nullptr && physicsProvider)
            {
                updateCharacterControllerEntity(entity, deltaTime);
            }
            else
            {
                updateRigidBodyEntity(entity, deltaTime);
            }

            // Derive locomotion state from physics results
            deriveLocomotionState(controller);

            // Sync locomotion state to animator parameters
            syncLocomotionToAnimator(entity, controller);

            // Reset per-frame input (scripts must set it again next frame)
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

        // Compute target speed
        float targetSpeed = controller.moveSpeed;
        if (controller.wantsSprint)
        {
            targetSpeed *= controller.sprintMultiplier;
        }

        // Compute desired horizontal velocity from input
        glm::vec3 desiredHorizontal{0.0f};
        if (glm::length2(controller.moveInput) > 0.001f)
        {
            glm::vec3 normalizedInput = glm::normalize(controller.moveInput);
            desiredHorizontal = normalizedInput * targetSpeed;
        }

        // Apply acceleration/deceleration smoothing on XZ plane
        glm::vec3 currentHorizontal{controller.currentVelocity.x, 0.0f, controller.currentVelocity.z};
        glm::vec3 velocityDiff = desiredHorizontal - currentHorizontal;
        float diffLen = glm::length(velocityDiff);

        if (diffLen > 0.001f)
        {
            // Determine if accelerating or decelerating
            float rate = glm::length2(desiredHorizontal) >= glm::length2(currentHorizontal)
                ? controller.acceleration
                : controller.deceleration;

            // Apply air control factor when airborne
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

        // Build full velocity: smoothed horizontal + vertical (gravity + jump)
        float verticalVel = controller.currentVelocity.y;

        // Apply gravity
        auto gravity = physicsProvider->getGravity();
        verticalVel += gravity.y * deltaTime;

        // Apply jump if grounded
        if (controller.wantsJump && controller.jumpForce > 0.0f && controller.isGrounded)
        {
            verticalVel = controller.jumpForce;
        }

        glm::vec3 fullVelocity{currentHorizontal.x, verticalVel, currentHorizontal.z};

        // Update character controller through physics
        auto result = physicsProvider->updateCharacterController(handle, fullVelocity, deltaTime);

        // Write back results
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

        // Apply movement from moveInput
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

        // Apply jump (only when grounded)
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
        if (!controller.isGrounded)
        {
            controller.locomotionState = controller.verticalVelocity > 0.1f
                ? components::LocomotionState::Jump
                : components::LocomotionState::Fall;
        }
        else if (controller.currentSpeed < 0.1f)
        {
            controller.locomotionState = components::LocomotionState::Idle;
        }
        else if (controller.currentSpeed < controller.walkSpeedThreshold)
        {
            controller.locomotionState = components::LocomotionState::Walk;
        }
        else
        {
            controller.locomotionState = components::LocomotionState::Run;
        }
    }

    void ControllerServiceImpl::syncLocomotionToAnimator(entt::entity entity,
                                                          const components::ControllerComponent& controller)
    {
        auto& registry = scene::EntityRegistry::getRegistry();
        if (!registry.all_of<components::AnimatorComponent>(entity)) return;

        const auto& animComp = registry.get<components::AnimatorComponent>(entity);
        if (!animComp.isInitialized) return;

        auto& dispatcher = ::events::EventDispatcher::instance();
        auto handle = internal::toHandle(entity);

        // Set animator parameters
        ::services::events::animator::SetEntityAnimatorFloatCommand speedCmd;
        speedCmd.entity = handle;
        speedCmd.parameterName = "speed";
        speedCmd.value = controller.currentSpeed;
        dispatcher.execute(speedCmd);

        ::services::events::animator::SetEntityAnimatorBoolCommand groundedCmd;
        groundedCmd.entity = handle;
        groundedCmd.parameterName = "grounded";
        groundedCmd.value = controller.isGrounded;
        dispatcher.execute(groundedCmd);

        ::services::events::animator::SetEntityAnimatorFloatCommand vertVelCmd;
        vertVelCmd.entity = handle;
        vertVelCmd.parameterName = "verticalVelocity";
        vertVelCmd.value = controller.verticalVelocity;
        dispatcher.execute(vertVelCmd);

        // Direction parameters for 2D blend trees
        if (glm::length2(controller.moveInput) > 0.001f)
        {
            ::services::events::animator::SetEntityAnimatorFloatCommand dirXCmd;
            dirXCmd.entity = handle;
            dirXCmd.parameterName = "directionX";
            dirXCmd.value = controller.moveInput.x;
            dispatcher.execute(dirXCmd);

            ::services::events::animator::SetEntityAnimatorFloatCommand dirYCmd;
            dirYCmd.entity = handle;
            dirYCmd.parameterName = "directionY";
            dirYCmd.value = controller.moveInput.z;
            dispatcher.execute(dirYCmd);
        }
    }
}
