// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "DebugDrawAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/DebugDrawEvents.hpp"

namespace core::api
{
    void DebugDrawAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // drawLine(sx, sy, sz, ex, ey, ez, r, g, b, a)
        interpreter->registerNativeFunction("_native_debugDraw_line",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                events::debugdraw::DrawLineCommand cmd;
                cmd.start = {extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])};
                cmd.end = {extractFloat(args[3]), extractFloat(args[4]), extractFloat(args[5])};
                cmd.color = {extractFloat(args[6]), extractFloat(args[7]), extractFloat(args[8]), extractFloat(args[9])};
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // drawRay(ox, oy, oz, dx, dy, dz, length, r, g, b, a)
        interpreter->registerNativeFunction("_native_debugDraw_ray",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                events::debugdraw::DrawRayCommand cmd;
                cmd.origin = {extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])};
                cmd.direction = {extractFloat(args[3]), extractFloat(args[4]), extractFloat(args[5])};
                cmd.length = extractFloat(args[6]);
                cmd.color = {extractFloat(args[7]), extractFloat(args[8]), extractFloat(args[9]), extractFloat(args[10])};
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // drawBox(cx, cy, cz, hx, hy, hz, r, g, b, a)
        interpreter->registerNativeFunction("_native_debugDraw_box",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                events::debugdraw::DrawBoxCommand cmd;
                cmd.center = {extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])};
                cmd.halfExtents = {extractFloat(args[3]), extractFloat(args[4]), extractFloat(args[5])};
                cmd.color = {extractFloat(args[6]), extractFloat(args[7]), extractFloat(args[8]), extractFloat(args[9])};
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // drawSphere(cx, cy, cz, radius, r, g, b, a)
        interpreter->registerNativeFunction("_native_debugDraw_sphere",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                events::debugdraw::DrawSphereCommand cmd;
                cmd.center = {extractFloat(args[0]), extractFloat(args[1]), extractFloat(args[2])};
                cmd.radius = extractFloat(args[3]);
                cmd.color = {extractFloat(args[4]), extractFloat(args[5]), extractFloat(args[6]), extractFloat(args[7])};
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // setEnabled(bool)
        interpreter->registerNativeFunction("_native_debugDraw_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                events::debugdraw::SetDebugDrawEnabledCommand cmd;
                cmd.enabled = extractBool(args[0]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // isEnabled() -> bool
        interpreter->registerNativeFunction("_native_debugDraw_isEnabled",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                return value::Value(dispatcher.query(events::debugdraw::GetDebugDrawEnabledQuery{}));
            });
    }
}
