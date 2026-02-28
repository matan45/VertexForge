#include "ControllerServiceImpl.hpp"
#include "../events/EventDispatcher.hpp"
#include "../events/ControllerEvents.hpp"
#include "../events/PhysicsEvents.hpp"
#include "../events/NavmeshEvents.hpp"
#include "../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/ControllerComponents.hpp"
#include "components/CoreComponents.hpp"
#include "components/NavmeshComponents.hpp"
#include "components/PhysicsComponents.hpp"
#include <glm/glm.hpp>

namespace services
{
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
                    // Apply sprint to navmesh agent speed
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
            controller.wantsJump = false;

            // Reset per-frame input (scripts must set it again next frame)
            if (!controller.hasMoveToTarget)
            {
                controller.moveInput = glm::vec3(0.0f);
            }
            controller.wantsSprint = false;
        }
    }
}
