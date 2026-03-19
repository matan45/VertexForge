#include "ControllerServiceImpl.hpp"
#include "../../events/EventDispatcher.hpp"
#include "../../events/physics/ControllerEvents.hpp"
#include "../../events/navmesh/NavmeshEvents.hpp"
#include "../../data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "components/ControllerComponents.hpp"
#include "components/Components.hpp"
#include "components/NavmeshComponents.hpp"
#include <glm/glm.hpp>

namespace services
{
    void ControllerServiceImpl::registerEventHandlers()
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        dispatcher.registerCommandHandler<::events::controller::SetMoveInputCommand>(
            [](const ::events::controller::SetMoveInputCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return;
                registry.get<components::ControllerComponent>(entity).moveInput = cmd.moveInput;
            });

        dispatcher.registerCommandHandler<::events::controller::SetJumpCommand>(
            [](const ::events::controller::SetJumpCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return;
                registry.get<components::ControllerComponent>(entity).wantsJump = cmd.wantsJump;
            });

        dispatcher.registerCommandHandler<::events::controller::SetSprintCommand>(
            [](const ::events::controller::SetSprintCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return;
                registry.get<components::ControllerComponent>(entity).wantsSprint = cmd.wantsSprint;
            });

        dispatcher.registerCommandHandler<::events::controller::MoveToCommand>(
            [](const ::events::controller::MoveToCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return false;
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
                    return;
                auto& controller = registry.get<components::ControllerComponent>(entity);
                controller.moveInput = glm::vec3(0.0f);
                controller.hasMoveToTarget = false;
                controller.moveToDestinationDirty = false;
                controller.wantsJump = false;
                controller.wantsSprint = false;

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
                    return;
                registry.get<components::ControllerComponent>(entity).moveSpeed = cmd.moveSpeed;
            });

        dispatcher.registerQueryHandler<::events::controller::HasReachedDestinationQuery>(
            [](const ::events::controller::HasReachedDestinationQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return true;
                auto& controller = registry.get<components::ControllerComponent>(entity);
                if (!controller.hasMoveToTarget)
                    return true;
                if (!registry.all_of<components::TransformComponent>(entity))
                    return true;
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
                    return 0.0f;
                auto& transform = registry.get<components::TransformComponent>(entity);
                return glm::length(query.target - transform.position);
            });

        dispatcher.registerQueryHandler<::events::controller::GetMoveSpeedQuery>(
            [](const ::events::controller::GetMoveSpeedQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return 0.0f;
                return registry.get<components::ControllerComponent>(entity).moveSpeed;
            });

        dispatcher.registerCommandHandler<::events::controller::SetJumpForceCommand>(
            [](const ::events::controller::SetJumpForceCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return;
                registry.get<components::ControllerComponent>(entity).jumpForce = cmd.jumpForce;
            });

        dispatcher.registerCommandHandler<::events::controller::SetSprintMultiplierCommand>(
            [](const ::events::controller::SetSprintMultiplierCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return;
                registry.get<components::ControllerComponent>(entity).sprintMultiplier = cmd.sprintMultiplier;
            });

        dispatcher.registerQueryHandler<::events::controller::GetJumpForceQuery>(
            [](const ::events::controller::GetJumpForceQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return 0.0f;
                return registry.get<components::ControllerComponent>(entity).jumpForce;
            });

        dispatcher.registerQueryHandler<::events::controller::GetSprintMultiplierQuery>(
            [](const ::events::controller::GetSprintMultiplierQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return 0.0f;
                return registry.get<components::ControllerComponent>(entity).sprintMultiplier;
            });

        dispatcher.registerCommandHandler<::events::controller::SetArrivalDistanceCommand>(
            [](const ::events::controller::SetArrivalDistanceCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return;
                registry.get<components::ControllerComponent>(entity).arrivalDistance = cmd.arrivalDistance;
            });

        dispatcher.registerQueryHandler<::events::controller::GetArrivalDistanceQuery>(
            [](const ::events::controller::GetArrivalDistanceQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return 0.5f;
                return registry.get<components::ControllerComponent>(entity).arrivalDistance;
            });

        dispatcher.registerCommandHandler<::events::controller::SetGroundedCommand>(
            [](const ::events::controller::SetGroundedCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return;
                registry.get<components::ControllerComponent>(entity).isGrounded = cmd.isGrounded;
            });

        dispatcher.registerQueryHandler<::events::controller::IsGroundedQuery>(
            [](const ::events::controller::IsGroundedQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return false;
                return registry.get<components::ControllerComponent>(entity).isGrounded;
            });

        dispatcher.registerCommandHandler<::events::controller::SetAccelerationCommand>(
            [](const ::events::controller::SetAccelerationCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return;
                registry.get<components::ControllerComponent>(entity).acceleration = cmd.acceleration;
            });

        dispatcher.registerCommandHandler<::events::controller::SetDecelerationCommand>(
            [](const ::events::controller::SetDecelerationCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return;
                registry.get<components::ControllerComponent>(entity).deceleration = cmd.deceleration;
            });

        dispatcher.registerCommandHandler<::events::controller::SetRotationSpeedCommand>(
            [](const ::events::controller::SetRotationSpeedCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return;
                registry.get<components::ControllerComponent>(entity).rotationSpeed = cmd.rotationSpeed;
            });

        dispatcher.registerCommandHandler<::events::controller::SetAirControlFactorCommand>(
            [](const ::events::controller::SetAirControlFactorCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return;
                registry.get<components::ControllerComponent>(entity).airControlFactor = cmd.airControlFactor;
            });

        dispatcher.registerCommandHandler<::events::controller::SetWalkSpeedThresholdCommand>(
            [](const ::events::controller::SetWalkSpeedThresholdCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return;
                registry.get<components::ControllerComponent>(entity).walkSpeedThreshold = cmd.walkSpeedThreshold;
            });

        dispatcher.registerCommandHandler<::events::controller::SetLocomotionStateCommand>(
            [](const ::events::controller::SetLocomotionStateCommand& cmd)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(cmd.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return;
                registry.get<components::ControllerComponent>(entity).locomotionState = cmd.state;
            });

        dispatcher.registerQueryHandler<::events::controller::GetLocomotionStateQuery>(
            [](const ::events::controller::GetLocomotionStateQuery& query) -> std::string
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return "Idle";
                return registry.get<components::ControllerComponent>(entity).locomotionState;
            });

        dispatcher.registerQueryHandler<::events::controller::GetCurrentSpeedQuery>(
            [](const ::events::controller::GetCurrentSpeedQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return 0.0f;
                return registry.get<components::ControllerComponent>(entity).currentSpeed;
            });

        dispatcher.registerQueryHandler<::events::controller::GetVerticalVelocityQuery>(
            [](const ::events::controller::GetVerticalVelocityQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return 0.0f;
                return registry.get<components::ControllerComponent>(entity).verticalVelocity;
            });

        dispatcher.registerQueryHandler<::events::controller::GetAccelerationQuery>(
            [](const ::events::controller::GetAccelerationQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return 0.0f;
                return registry.get<components::ControllerComponent>(entity).acceleration;
            });

        dispatcher.registerQueryHandler<::events::controller::GetDecelerationQuery>(
            [](const ::events::controller::GetDecelerationQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return 0.0f;
                return registry.get<components::ControllerComponent>(entity).deceleration;
            });

        dispatcher.registerQueryHandler<::events::controller::GetRotationSpeedQuery>(
            [](const ::events::controller::GetRotationSpeedQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return 0.0f;
                return registry.get<components::ControllerComponent>(entity).rotationSpeed;
            });

        dispatcher.registerQueryHandler<::events::controller::GetAirControlFactorQuery>(
            [](const ::events::controller::GetAirControlFactorQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return 0.0f;
                return registry.get<components::ControllerComponent>(entity).airControlFactor;
            });

        dispatcher.registerQueryHandler<::events::controller::GetWalkSpeedThresholdQuery>(
            [](const ::events::controller::GetWalkSpeedThresholdQuery& query)
            {
                auto& registry = scene::EntityRegistry::getRegistry();
                auto entity = internal::fromHandle(query.entity);
                if (!registry.valid(entity) || !registry.all_of<components::ControllerComponent>(entity))
                    return 0.0f;
                return registry.get<components::ControllerComponent>(entity).walkSpeedThreshold;
            });
    }
}
