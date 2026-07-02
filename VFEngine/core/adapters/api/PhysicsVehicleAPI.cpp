// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>

#include "PhysicsVehicleAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/physics/PhysicsEvents.hpp"

namespace core::api
{
    namespace
    {
        constexpr size_t kWheelStateArraySize = 22;

        value::Value makeWheelStateArray(const std::optional<types::WheelState>& stateOpt)
        {
            auto arr = std::make_shared<value::NativeArray>(kWheelStateArraySize, value::ValueType::FLOAT);
            for (size_t i = 0; i < kWheelStateArraySize; ++i)
            {
                arr->set(i, value::Value(0.0f));
            }

            if (!stateOpt.has_value())
            {
                return value::Value(arr);
            }

            const auto& state = *stateOpt;
            arr->set(0, value::Value(1.0f));
            arr->set(1, value::Value(state.rotationAngle));
            arr->set(2, value::Value(state.steerAngle));
            arr->set(3, value::Value(state.suspensionLength));
            arr->set(4, value::Value(state.angularVelocity));
            arr->set(5, value::Value(state.hasContact ? 1.0f : 0.0f));

            for (int row = 0; row < 4; ++row)
            {
                for (int col = 0; col < 4; ++col)
                {
                    arr->set(6 + row * 4 + col, value::Value(state.worldTransform[col][row]));
                }
            }

            return value::Value(arr);
        }

        std::optional<services::EntityHandle> readEntityArg(std::span<const value::Value> args)
        {
            if (args.empty())
            {
                return std::nullopt;
            }

            const int64_t id = extractInt64(args[0]);
            if (id < 0)
            {
                return std::nullopt;
            }

            return intToEntity(id);
        }
    }

    void PhysicsVehicleAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        interpreter->registerNativeFunction("_native_physics_createVehicle",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                auto entity = readEntityArg(args);
                if (!entity.has_value()) return value::Value(false);

                events::physics::CreateVehicleCommand cmd;
                cmd.entity = *entity;
                cmd.rebuild = false;
                return value::Value(events::EventDispatcher::instance().execute(cmd));
            }});

        interpreter->registerNativeFunction("_native_physics_rebuildVehicle",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                auto entity = readEntityArg(args);
                if (!entity.has_value()) return value::Value(false);

                events::physics::CreateVehicleCommand cmd;
                cmd.entity = *entity;
                cmd.rebuild = true;
                return value::Value(events::EventDispatcher::instance().execute(cmd));
            }});

        interpreter->registerNativeFunction("_native_physics_destroyVehicle",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                auto entity = readEntityArg(args);
                if (!entity.has_value()) return value::Value(false);

                events::physics::DestroyVehicleCommand cmd;
                cmd.entity = *entity;
                return value::Value(events::EventDispatcher::instance().execute(cmd));
            }});

        interpreter->registerNativeFunction("_native_physics_hasVehicle",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                auto entity = readEntityArg(args);
                if (!entity.has_value()) return value::Value(false);

                events::physics::HasVehicleQuery query;
                query.entity = *entity;
                return value::Value(events::EventDispatcher::instance().query(query));
            }});

        interpreter->registerNativeFunction("_native_physics_setVehicleInput",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                auto entity = readEntityArg(args);
                if (!entity.has_value() || args.size() < 5) return value::Value(std::monostate{});

                events::physics::SetVehicleInputCommand cmd;
                cmd.entity = *entity;
                cmd.throttle = extractFloat(args[1]);
                cmd.steer = extractFloat(args[2]);
                cmd.brake = extractFloat(args[3]);
                cmd.handbrake = extractFloat(args[4]);
                events::EventDispatcher::instance().execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_physics_getVehicleWheelCount",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                auto entity = readEntityArg(args);
                if (!entity.has_value()) return value::Value(static_cast<int64_t>(0));

                events::physics::GetVehicleWheelStatesQuery query;
                query.entity = *entity;
                auto states = events::EventDispatcher::instance().query(query);
                return value::Value(static_cast<int64_t>(states.size()));
            }});

        interpreter->registerNativeFunction("_native_physics_getVehicleWheelState",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value {
                auto entity = readEntityArg(args);
                if (!entity.has_value() || args.size() < 2)
                {
                    return makeWheelStateArray(std::nullopt);
                }

                events::physics::GetVehicleWheelStateQuery query;
                query.entity = *entity;
                query.wheelIndex = static_cast<int>(extractInt64(args[1]));
                return makeWheelStateArray(events::EventDispatcher::instance().query(query));
            }});

        vfLogInfo("[PhysicsVehicleAPI] Registered PhysicsVehicle native functions");
    }
}
