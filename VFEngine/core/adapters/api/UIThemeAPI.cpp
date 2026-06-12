// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "UIThemeAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIThemeEvents.hpp"

namespace core::api
{
    void UIThemeAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        interpreter->registerNativeFunction("_native_ui_setCanvasTheme",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(false);

                events::ui::SetCanvasThemeCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0], "_native_ui_setCanvasTheme"));
                cmd.themePath = extractString(args[1], "_native_ui_setCanvasTheme");
                return value::Value(dispatcher.execute(cmd));
            }});

        interpreter->registerNativeFunction("_native_ui_getCanvasTheme",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::string(""));

                events::ui::GetCanvasThemeQuery query;
                query.entity = intToEntity(extractInt64(args[0], "_native_ui_getCanvasTheme"));
                auto path = dispatcher.query(query);
                return value::Value(path.has_value() ? *path : std::string(""));
            }});

        interpreter->registerNativeFunction("_native_ui_setStyleKey",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(false);

                events::ui::SetUIStyleKeyCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0], "_native_ui_setStyleKey"));
                cmd.styleKey = extractString(args[1], "_native_ui_setStyleKey");
                return value::Value(dispatcher.execute(cmd));
            }});

        interpreter->registerNativeFunction("_native_ui_getStyleKey",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::string(""));

                events::ui::GetUIStyleKeyQuery query;
                query.entity = intToEntity(extractInt64(args[0], "_native_ui_getStyleKey"));
                auto key = dispatcher.query(query);
                return value::Value(key.has_value() ? *key : std::string(""));
            }});

        // canvasId < 0 reapplies every themed canvas. Returns touched count.
        interpreter->registerNativeFunction("_native_ui_reapplyTheme",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();

                events::ui::ReapplyUIThemeCommand cmd;
                if (!args.empty())
                {
                    int64_t id = extractInt64(args[0], "_native_ui_reapplyTheme");
                    if (id >= 0)
                    {
                        cmd.canvas = intToEntity(id);
                    }
                }
                return value::Value(static_cast<int64_t>(dispatcher.execute(cmd)));
            }});
    }
}
