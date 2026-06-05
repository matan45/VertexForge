// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "WindowAPI.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/project/ApplicationEvents.hpp"

namespace core::api
{
    void WindowAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        // _native_window_getWidth() -> int
        interpreter->registerNativeFunction("_native_window_getWidth",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                uint32_t w = dispatcher.query(events::application::GetWindowWidthQuery{});
                return value::Value(static_cast<int64_t>(w));
            }});

        // _native_window_getHeight() -> int
        interpreter->registerNativeFunction("_native_window_getHeight",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                uint32_t h = dispatcher.query(events::application::GetWindowHeightQuery{});
                return value::Value(static_cast<int64_t>(h));
            }});

        // _native_window_getViewportWidth() -> int
        // Editor play mode: active ViewPort panel width. Standalone runtime / non-play: full window width.
        interpreter->registerNativeFunction("_native_window_getViewportWidth",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                uint32_t w = dispatcher.query(events::application::GetViewportWidthQuery{});
                return value::Value(static_cast<int64_t>(w));
            }});

        // _native_window_getViewportHeight() -> int
        interpreter->registerNativeFunction("_native_window_getViewportHeight",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                uint32_t h = dispatcher.query(events::application::GetViewportHeightQuery{});
                return value::Value(static_cast<int64_t>(h));
            }});
    }
}
