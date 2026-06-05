// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

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
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    return value::Value(false);
                }
                int keyCode = static_cast<int>(extractInt64(args[0]));

                events::input::IsKeyDownQuery query;
                query.keyCode = keyCode;
                return value::Value(dispatcher.query(query));
            }});

        // _native_input_isMouseButtonDown(button) -> bool
        interpreter->registerNativeFunction("_native_input_isMouseButtonDown",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    return value::Value(false);
                }
                int button = static_cast<int>(extractInt64(args[0]));

                events::input::IsMouseButtonDownQuery query;
                query.button = button;
                return value::Value(dispatcher.query(query));
            }});

        // _native_input_getMouseX() -> float
        interpreter->registerNativeFunction("_native_input_getMouseX",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::input::GetMousePositionQuery query;
                glm::vec2 pos = dispatcher.query(query);
                return value::Value(pos.x);
            }});

        // _native_input_getMouseY() -> float
        interpreter->registerNativeFunction("_native_input_getMouseY",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::input::GetMousePositionQuery query;
                glm::vec2 pos = dispatcher.query(query);
                return value::Value(pos.y);
            }});

        // _native_input_getViewportMouseX() -> float
        // Editor play mode: panel-relative. Standalone runtime / non-play: same as getMouseX.
        interpreter->registerNativeFunction("_native_input_getViewportMouseX",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::input::GetViewportMousePositionQuery query;
                glm::vec2 pos = dispatcher.query(query);
                return value::Value(pos.x);
            }});

        // _native_input_getViewportMouseY() -> float
        interpreter->registerNativeFunction("_native_input_getViewportMouseY",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::input::GetViewportMousePositionQuery query;
                glm::vec2 pos = dispatcher.query(query);
                return value::Value(pos.y);
            }});

        // _native_input_getMouseDeltaX() -> float
        interpreter->registerNativeFunction("_native_input_getMouseDeltaX",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::input::GetMouseDeltaQuery query;
                glm::vec2 delta = dispatcher.query(query);
                return value::Value(delta.x);
            }});

        // _native_input_getMouseDeltaY() -> float
        interpreter->registerNativeFunction("_native_input_getMouseDeltaY",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::input::GetMouseDeltaQuery query;
                glm::vec2 delta = dispatcher.query(query);
                return value::Value(delta.y);
            }});

        // _native_input_getMouseScrollDeltaX() -> float
        interpreter->registerNativeFunction("_native_input_getMouseScrollDeltaX",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::input::GetScrollDeltaQuery query;
                glm::vec2 delta = dispatcher.query(query);
                return value::Value(delta.x);
            }});

        // _native_input_getMouseScrollDeltaY() -> float
        interpreter->registerNativeFunction("_native_input_getMouseScrollDeltaY",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::input::GetScrollDeltaQuery query;
                glm::vec2 delta = dispatcher.query(query);
                return value::Value(delta.y);
            }});

        // _native_input_isKeyReleased(keyCode) -> bool
        interpreter->registerNativeFunction("_native_input_isKeyReleased",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    return value::Value(false);
                }
                int keyCode = static_cast<int>(extractInt64(args[0]));

                events::input::IsKeyReleasedQuery query;
                query.keyCode = keyCode;
                return value::Value(dispatcher.query(query));
            }});

        // _native_input_isMouseButtonReleased(button) -> bool
        interpreter->registerNativeFunction("_native_input_isMouseButtonReleased",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    return value::Value(false);
                }
                int button = static_cast<int>(extractInt64(args[0]));

                events::input::IsMouseButtonReleasedQuery query;
                query.button = button;
                return value::Value(dispatcher.query(query));
            }});

        // _native_input_isDoubleClick(button) -> bool
        interpreter->registerNativeFunction("_native_input_isDoubleClick",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty())
                {
                    return value::Value(false);
                }
                int button = static_cast<int>(extractInt64(args[0]));

                events::input::IsDoubleClickQuery query;
                query.button = button;
                return value::Value(dispatcher.query(query));
            }});
        // _native_input_setKeyboardEnabled(enabled) -> void
        interpreter->registerNativeFunction("_native_input_setKeyboardEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                bool enabled = extractBool(args[0]);
                events::input::SetKeyboardEnabledCommand cmd;
                cmd.enabled = enabled;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_input_setMouseEnabled(enabled) -> void
        interpreter->registerNativeFunction("_native_input_setMouseEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                bool enabled = extractBool(args[0]);
                events::input::SetMouseEnabledCommand cmd;
                cmd.enabled = enabled;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_input_setCursorVisible(visible) -> void
        interpreter->registerNativeFunction("_native_input_setCursorVisible",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::monostate{});
                bool visible = extractBool(args[0]);
                events::input::SetCursorVisibleCommand cmd;
                cmd.visible = visible;
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // _native_input_isKeyboardEnabled() -> bool
        interpreter->registerNativeFunction("_native_input_isKeyboardEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return value::Value(dispatcher.query(events::input::IsKeyboardEnabledQuery{}));
            }});

        // _native_input_isMouseEnabled() -> bool
        interpreter->registerNativeFunction("_native_input_isMouseEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return value::Value(dispatcher.query(events::input::IsMouseEnabledQuery{}));
            }});

        // _native_input_isCursorVisible() -> bool
        interpreter->registerNativeFunction("_native_input_isCursorVisible",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return value::Value(dispatcher.query(events::input::IsCursorVisibleQuery{}));
            }});
    }
}
