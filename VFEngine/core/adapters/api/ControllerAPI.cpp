// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "ControllerAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/physics/ControllerEvents.hpp"

namespace core::api
{
    void ControllerAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        interpreter->registerNativeFunction("_native_controller_setMoveInput",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 4) return value::Value(std::monostate{});

                ::events::controller::SetMoveInputCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.moveInput = glm::vec3(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3]));
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_controller_setJump",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetJumpCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.wantsJump = extractBool(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_controller_setSprint",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetSprintCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.wantsSprint = extractBool(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_controller_moveTo",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 4) return value::Value(false);

                ::events::controller::MoveToCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.destination = glm::vec3(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3]));
                return value::Value(dispatcher.execute(cmd));
            }});

        interpreter->registerNativeFunction("_native_controller_stopMovement",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});

                ::events::controller::StopMovementCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_controller_hasReachedDestination",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(true);

                ::events::controller::HasReachedDestinationQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_getDistanceTo",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 4) return value::Value(0.0f);

                ::events::controller::GetDistanceToQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                query.target = glm::vec3(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_getMoveSpeed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(0.0f);

                ::events::controller::GetMoveSpeedQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_setMoveSpeed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetMoveSpeedCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.moveSpeed = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_controller_getJumpForce",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(0.0f);

                ::events::controller::GetJumpForceQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_setJumpForce",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetJumpForceCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.jumpForce = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_controller_getSprintMultiplier",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(0.0f);

                ::events::controller::GetSprintMultiplierQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_setSprintMultiplier",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetSprintMultiplierCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.sprintMultiplier = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_controller_getArrivalDistance",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(0.5f);

                ::events::controller::GetArrivalDistanceQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_setArrivalDistance",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetArrivalDistanceCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.arrivalDistance = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_controller_isGrounded",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(false);

                ::events::controller::IsGroundedQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_setGrounded",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetGroundedCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.isGrounded = extractBool(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // Locomotion state queries

        interpreter->registerNativeFunction("_native_controller_getLocomotionState",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::string("Idle"));

                ::events::controller::GetLocomotionStateQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_setLocomotionState",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetLocomotionStateCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.state = extractString(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_controller_getCurrentSpeed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(0.0f);

                ::events::controller::GetCurrentSpeedQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_getVerticalVelocity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(0.0f);

                ::events::controller::GetVerticalVelocityQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        // Locomotion settings

        interpreter->registerNativeFunction("_native_controller_getAcceleration",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(0.0f);

                ::events::controller::GetAccelerationQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_setAcceleration",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetAccelerationCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.acceleration = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_controller_getDeceleration",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(0.0f);

                ::events::controller::GetDecelerationQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_setDeceleration",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetDecelerationCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.deceleration = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_controller_getRotationSpeed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(0.0f);

                ::events::controller::GetRotationSpeedQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_setRotationSpeed",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetRotationSpeedCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.rotationSpeed = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_controller_getAirControl",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(0.0f);

                ::events::controller::GetAirControlFactorQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_setAirControl",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetAirControlFactorCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.airControlFactor = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_controller_getWalkRunThreshold",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(0.0f);

                ::events::controller::GetWalkSpeedThresholdQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_controller_setWalkRunThreshold",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetWalkSpeedThresholdCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.walkSpeedThreshold = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});
    }
}
