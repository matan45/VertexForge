// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "UIWidgetExtrasAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UITooltipEvents.hpp"

namespace core::api
{
    namespace
    {
        std::optional<services::UITooltipData> getTooltipData(events::EventDispatcher& dispatcher,
                                                              services::EntityHandle handle)
        {
            events::ui::GetUITooltipDataQuery query;
            query.entity = handle;
            return dispatcher.query(query);
        }

        void setTooltipData(events::EventDispatcher& dispatcher, services::EntityHandle handle,
                            const services::UITooltipData& data)
        {
            events::ui::SetUITooltipDataCommand cmd;
            cmd.entity = handle;
            cmd.tooltipData = data;
            dispatcher.execute(cmd);
        }
    }

    void UIWidgetExtrasAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        interpreter->registerNativeFunction("_native_ui_setTooltipText",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setTooltipText"));

                auto data = getTooltipData(dispatcher, handle);
                if (!data.has_value()) return value::Value();
                data->text = extractString(args[1], "_native_ui_setTooltipText");
                setTooltipData(dispatcher, handle, *data);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_getTooltipText",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::string(""));
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getTooltipText"));

                auto data = getTooltipData(dispatcher, handle);
                return value::Value(data.has_value() ? data->text : std::string(""));
            }});

        interpreter->registerNativeFunction("_native_ui_setTooltipEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setTooltipEnabled"));

                auto data = getTooltipData(dispatcher, handle);
                if (!data.has_value()) return value::Value();
                data->enabled = extractBool(args[1]);
                setTooltipData(dispatcher, handle, *data);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_setTooltipDelay",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setTooltipDelay"));

                auto data = getTooltipData(dispatcher, handle);
                if (!data.has_value()) return value::Value();
                data->showDelay = extractFloat(args[1], "_native_ui_setTooltipDelay");
                setTooltipData(dispatcher, handle, *data);
                return value::Value();
            }});
    }
}
