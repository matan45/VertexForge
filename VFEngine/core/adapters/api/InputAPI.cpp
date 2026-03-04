// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "InputAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/input/InputEvents.hpp"

namespace core::api
{
    void InputAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_input_isKeyDown(keyCode) -> bool
        interpreter->registerNativeFunction("_native_input_isKeyDown",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(false);
                }
                int keyCode = static_cast<int>(extractInt64(args[0]));

                events::input::IsKeyDownQuery query;
                query.keyCode = keyCode;
                return value::Value(dispatcher.query(query));
            });

        // _native_input_isMouseButtonDown(button) -> bool
        interpreter->registerNativeFunction("_native_input_isMouseButtonDown",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(false);
                }
                int button = static_cast<int>(extractInt64(args[0]));

                events::input::IsMouseButtonDownQuery query;
                query.button = button;
                return value::Value(dispatcher.query(query));
            });

        // _native_input_getMouseX() -> float
        interpreter->registerNativeFunction("_native_input_getMouseX",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                events::input::GetMousePositionQuery query;
                glm::vec2 pos = dispatcher.query(query);
                return value::Value(pos.x);
            });

        // _native_input_getMouseY() -> float
        interpreter->registerNativeFunction("_native_input_getMouseY",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                events::input::GetMousePositionQuery query;
                glm::vec2 pos = dispatcher.query(query);
                return value::Value(pos.y);
            });

        // _native_input_getMouseDeltaX() -> float
        interpreter->registerNativeFunction("_native_input_getMouseDeltaX",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                events::input::GetMouseDeltaQuery query;
                glm::vec2 delta = dispatcher.query(query);
                return value::Value(delta.x);
            });

        // _native_input_getMouseDeltaY() -> float
        interpreter->registerNativeFunction("_native_input_getMouseDeltaY",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                events::input::GetMouseDeltaQuery query;
                glm::vec2 delta = dispatcher.query(query);
                return value::Value(delta.y);
            });

        // _native_input_isKeyReleased(keyCode) -> bool
        interpreter->registerNativeFunction("_native_input_isKeyReleased",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(false);
                }
                int keyCode = static_cast<int>(extractInt64(args[0]));

                events::input::IsKeyReleasedQuery query;
                query.keyCode = keyCode;
                return value::Value(dispatcher.query(query));
            });

        // _native_input_isMouseButtonReleased(button) -> bool
        interpreter->registerNativeFunction("_native_input_isMouseButtonReleased",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(false);
                }
                int button = static_cast<int>(extractInt64(args[0]));

                events::input::IsMouseButtonReleasedQuery query;
                query.button = button;
                return value::Value(dispatcher.query(query));
            });

        // _native_input_isDoubleClick(button) -> bool
        interpreter->registerNativeFunction("_native_input_isDoubleClick",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(false);
                }
                int button = static_cast<int>(extractInt64(args[0]));

                events::input::IsDoubleClickQuery query;
                query.button = button;
                return value::Value(dispatcher.query(query));
            });
    }
}
