// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "ControllerAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ControllerEvents.hpp"

namespace core::api
{
    void ControllerAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = ::events::EventDispatcher::instance();

        // _native_controller_setMoveInput(entityId, x, y, z) -> void
        interpreter->registerNativeFunction("_native_controller_setMoveInput",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 4) return value::Value(std::monostate{});

                ::events::controller::SetMoveInputCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.moveInput = glm::vec3(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3]));
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_controller_setJump(entityId, wantsJump) -> void
        interpreter->registerNativeFunction("_native_controller_setJump",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetJumpCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.wantsJump = extractBool(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_controller_setSprint(entityId, wantsSprint) -> void
        interpreter->registerNativeFunction("_native_controller_setSprint",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetSprintCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.wantsSprint = extractBool(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_controller_moveTo(entityId, x, y, z) -> bool
        interpreter->registerNativeFunction("_native_controller_moveTo",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 4) return value::Value(false);

                ::events::controller::MoveToCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.destination = glm::vec3(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3]));
                return value::Value(dispatcher.execute(cmd));
            });

        // _native_controller_stopMovement(entityId) -> void
        interpreter->registerNativeFunction("_native_controller_stopMovement",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});

                ::events::controller::StopMovementCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_controller_hasReachedDestination(entityId) -> bool
        interpreter->registerNativeFunction("_native_controller_hasReachedDestination",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(true);

                ::events::controller::HasReachedDestinationQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            });

        // _native_controller_getDistanceTo(entityId, x, y, z) -> float
        interpreter->registerNativeFunction("_native_controller_getDistanceTo",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 4) return value::Value(0.0f);

                ::events::controller::GetDistanceToQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                query.target = glm::vec3(extractFloat(args[1]), extractFloat(args[2]), extractFloat(args[3]));
                return value::Value(dispatcher.query(query));
            });

        // _native_controller_getMoveSpeed(entityId) -> float
        interpreter->registerNativeFunction("_native_controller_getMoveSpeed",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(0.0f);

                ::events::controller::GetMoveSpeedQuery query;
                query.entity = intToEntity(extractInt64(args[0]));
                return value::Value(dispatcher.query(query));
            });

        // _native_controller_setMoveSpeed(entityId, speed) -> void
        interpreter->registerNativeFunction("_native_controller_setMoveSpeed",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value(std::monostate{});

                ::events::controller::SetMoveSpeedCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0]));
                cmd.moveSpeed = extractFloat(args[1]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });
    }
}
