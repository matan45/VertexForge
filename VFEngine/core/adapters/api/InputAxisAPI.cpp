// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "InputAxisAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/input/ActionMappingEvents.hpp"

namespace core::api
{
    void InputAxisAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_inputaxis_getValue1D(name) -> float
        interpreter->registerNativeFunction("_native_inputaxis_getValue1D",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(0.0);
                std::string name = extractString(args[0], "_native_inputaxis_getValue1D");

                events::input::GetAxis1DValueQuery query;
                query.axisName = name;
                return value::Value(static_cast<double>(dispatcher.query(query)));
            });

        // _native_inputaxis_getValue2DX(name) -> float
        interpreter->registerNativeFunction("_native_inputaxis_getValue2DX",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(0.0);
                std::string name = extractString(args[0], "_native_inputaxis_getValue2DX");

                events::input::GetAxis2DValueQuery query;
                query.axisName = name;
                auto result = dispatcher.query(query);
                return value::Value(static_cast<double>(result.x));
            });

        // _native_inputaxis_getValue2DY(name) -> float
        interpreter->registerNativeFunction("_native_inputaxis_getValue2DY",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(0.0);
                std::string name = extractString(args[0], "_native_inputaxis_getValue2DY");

                events::input::GetAxis2DValueQuery query;
                query.axisName = name;
                auto result = dispatcher.query(query);
                return value::Value(static_cast<double>(result.y));
            });

        // _native_inputaxis_register1D(name, positiveAction, negativeAction)
        interpreter->registerNativeFunction("_native_inputaxis_register1D",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 3) return value::Value(std::monostate{});
                std::string name = extractString(args[0], "_native_inputaxis_register1D");
                std::string positive = extractString(args[1], "_native_inputaxis_register1D");
                std::string negative = extractString(args[2], "_native_inputaxis_register1D");

                events::input::RegisterAxis1DCommand cmd;
                cmd.axisName = name;
                cmd.positiveAction = positive;
                cmd.negativeAction = negative;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_inputaxis_register2D(name, up, down, left, right, normalize)
        interpreter->registerNativeFunction("_native_inputaxis_register2D",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 5) return value::Value(std::monostate{});
                std::string name = extractString(args[0], "_native_inputaxis_register2D");
                std::string up = extractString(args[1], "_native_inputaxis_register2D");
                std::string down = extractString(args[2], "_native_inputaxis_register2D");
                std::string left = extractString(args[3], "_native_inputaxis_register2D");
                std::string right = extractString(args[4], "_native_inputaxis_register2D");
                bool normalize = args.size() > 5 ? extractBool(args[5]) : true;

                events::input::RegisterAxis2DCommand cmd;
                cmd.axisName = name;
                cmd.upAction = up;
                cmd.downAction = down;
                cmd.leftAction = left;
                cmd.rightAction = right;
                cmd.normalize = normalize;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_inputaxis_unregister1D(name)
        interpreter->registerNativeFunction("_native_inputaxis_unregister1D",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});
                std::string name = extractString(args[0], "_native_inputaxis_unregister1D");

                events::input::UnregisterAxis1DCommand cmd;
                cmd.axisName = name;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // _native_inputaxis_unregister2D(name)
        interpreter->registerNativeFunction("_native_inputaxis_unregister2D",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::monostate{});
                std::string name = extractString(args[0], "_native_inputaxis_unregister2D");

                events::input::UnregisterAxis2DCommand cmd;
                cmd.axisName = name;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });
    }
}
