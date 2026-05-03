// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "DebugDrawAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/render/DebugDrawEvents.hpp"

namespace core::api
{
    void DebugDrawAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        interpreter->registerNativeFunction("_native_debugDraw_line",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::debugdraw::DrawLineCommand cmd;
                cmd.start = {extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])};
                cmd.end = {extractFloat(args[3]), extractFloat(args[4]), extractFloat(args[5])};
                cmd.color = {extractFloat(args[6]), extractFloat(args[7]), extractFloat(args[8]), extractFloat(args[9])};
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_debugDraw_ray",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::debugdraw::DrawRayCommand cmd;
                cmd.origin = {extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])};
                cmd.direction = {extractFloat(args[3]), extractFloat(args[4]), extractFloat(args[5])};
                cmd.length = extractFloat(args[6]);
                cmd.color = {extractFloat(args[7]), extractFloat(args[8]), extractFloat(args[9]), extractFloat(args[10])};
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_debugDraw_box",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::debugdraw::DrawBoxCommand cmd;
                cmd.center = {extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])};
                cmd.halfExtents = {extractFloat(args[3]), extractFloat(args[4]), extractFloat(args[5])};
                cmd.color = {extractFloat(args[6]), extractFloat(args[7]), extractFloat(args[8]), extractFloat(args[9])};
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_debugDraw_sphere",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::debugdraw::DrawSphereCommand cmd;
                cmd.center = {extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])};
                cmd.radius = extractFloat(args[3]);
                cmd.color = {extractFloat(args[4]), extractFloat(args[5]), extractFloat(args[6]), extractFloat(args[7])};
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_debugDraw_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::debugdraw::SetDebugDrawEnabledCommand cmd;
                cmd.enabled = extractBool(args[0]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        interpreter->registerNativeFunction("_native_debugDraw_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return value::Value(dispatcher.query(events::debugdraw::GetDebugDrawEnabledQuery{}));
            }});
    }
}
