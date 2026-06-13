// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "UIWidgetExtrasAPI.hpp"
#include "NativeHelpers.hpp"
#include "asset/AssetRef.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UITooltipEvents.hpp"
#include "../../../services/events/ui/UIWindowEvents.hpp"
#include "../../../services/events/ui/UIListViewEvents.hpp"

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

        interpreter->registerNativeFunction("_native_ui_setTooltipFont",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setTooltipFont"));

                auto data = getTooltipData(dispatcher, handle);
                if (!data.has_value()) return value::Value();
                data->fontRef = asset::AssetRef::fromPath(extractString(args[1], "_native_ui_setTooltipFont"));
                setTooltipData(dispatcher, handle, *data);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_setTooltipFontSize",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setTooltipFontSize"));

                auto data = getTooltipData(dispatcher, handle);
                if (!data.has_value()) return value::Value();
                data->fontSize = extractFloat(args[1], "_native_ui_setTooltipFontSize");
                setTooltipData(dispatcher, handle, *data);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_setTooltipLetterSpacing",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setTooltipLetterSpacing"));

                auto data = getTooltipData(dispatcher, handle);
                if (!data.has_value()) return value::Value();
                data->letterSpacing = extractFloat(args[1], "_native_ui_setTooltipLetterSpacing");
                setTooltipData(dispatcher, handle, *data);
                return value::Value();
            }});

        // padding(left, right, top, bottom) in pixels. The Text-mode bubble hugs
        // its text, so left/right padding is the lever for a wider bubble.
        interpreter->registerNativeFunction("_native_ui_setTooltipPadding",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 5) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setTooltipPadding"));

                auto data = getTooltipData(dispatcher, handle);
                if (!data.has_value()) return value::Value();
                data->padding = glm::vec4(
                    extractFloat(args[1], "_native_ui_setTooltipPadding"),
                    extractFloat(args[2], "_native_ui_setTooltipPadding"),
                    extractFloat(args[3], "_native_ui_setTooltipPadding"),
                    extractFloat(args[4], "_native_ui_setTooltipPadding"));
                setTooltipData(dispatcher, handle, *data);
                return value::Value();
            }});

        // ===== Window =====

        interpreter->registerNativeFunction("_native_ui_openWindow",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(false);
                events::ui::OpenUIWindowCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0], "_native_ui_openWindow"));
                return value::Value(dispatcher.execute(cmd));
            }});

        interpreter->registerNativeFunction("_native_ui_closeWindow",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(false);
                events::ui::CloseUIWindowCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0], "_native_ui_closeWindow"));
                return value::Value(dispatcher.execute(cmd));
            }});

        interpreter->registerNativeFunction("_native_ui_isWindowOpen",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(false);
                events::ui::IsUIWindowOpenQuery query;
                query.entity = intToEntity(extractInt64(args[0], "_native_ui_isWindowOpen"));
                return value::Value(dispatcher.query(query));
            }});

        interpreter->registerNativeFunction("_native_ui_setWindowModal",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setWindowModal"));

                events::ui::GetUIWindowDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();
                data->modal = extractBool(args[1]);

                events::ui::SetUIWindowDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.windowData = *data;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_setWindowTitle",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setWindowTitle"));

                events::ui::GetUIWindowDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();
                data->title = extractString(args[1], "_native_ui_setWindowTitle");

                events::ui::SetUIWindowDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.windowData = *data;
                dispatcher.execute(setCmd);
                return value::Value();
            }});

        interpreter->registerNativeFunction("_native_ui_getWindowTitle",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(std::string(""));
                events::ui::GetUIWindowDataQuery query;
                query.entity = intToEntity(extractInt64(args[0], "_native_ui_getWindowTitle"));
                auto data = dispatcher.query(query);
                return value::Value(data.has_value() ? data->title : std::string(""));
            }});

        // ===== ListView =====

        interpreter->registerNativeFunction("_native_ui_setListItemCount",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(false);
                events::ui::SetUIListItemCountCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0], "_native_ui_setListItemCount"));
                cmd.itemCount = static_cast<int>(extractInt64(args[1], "_native_ui_setListItemCount"));
                return value::Value(dispatcher.execute(cmd));
            }});

        interpreter->registerNativeFunction("_native_ui_getListItemCount",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(static_cast<int64_t>(0));
                events::ui::GetUIListViewDataQuery query;
                query.entity = intToEntity(extractInt64(args[0], "_native_ui_getListItemCount"));
                auto data = dispatcher.query(query);
                return value::Value(static_cast<int64_t>(data.has_value() ? data->itemCount : 0));
            }});

        interpreter->registerNativeFunction("_native_ui_getListItem",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(static_cast<int64_t>(-1));
                events::ui::GetUIListItemQuery query;
                query.entity = intToEntity(extractInt64(args[0], "_native_ui_getListItem"));
                query.index = static_cast<int>(extractInt64(args[1], "_native_ui_getListItem"));
                auto item = dispatcher.query(query);
                if (!item.isValid()) return value::Value(static_cast<int64_t>(-1));
                return value::Value(static_cast<int64_t>(item.id));
            }});

        interpreter->registerNativeFunction("_native_ui_setListItemTemplate",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(false);
                events::ui::SetUIListItemTemplateCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0], "_native_ui_setListItemTemplate"));
                cmd.templatePath = extractString(args[1], "_native_ui_setListItemTemplate");
                return value::Value(dispatcher.execute(cmd));
            }});

        interpreter->registerNativeFunction("_native_ui_getListSelectedIndex",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.empty()) return value::Value(static_cast<int64_t>(-1));
                events::ui::GetUIListViewDataQuery query;
                query.entity = intToEntity(extractInt64(args[0], "_native_ui_getListSelectedIndex"));
                auto data = dispatcher.query(query);
                return value::Value(static_cast<int64_t>(data.has_value() ? data->selectedIndex : -1));
            }});

        interpreter->registerNativeFunction("_native_ui_setListSelectedIndex",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                if (args.size() < 2) return value::Value(false);
                events::ui::SetUIListSelectedIndexCommand cmd;
                cmd.entity = intToEntity(extractInt64(args[0], "_native_ui_setListSelectedIndex"));
                cmd.selectedIndex = static_cast<int>(extractInt64(args[1], "_native_ui_setListSelectedIndex"));
                return value::Value(dispatcher.execute(cmd));
            }});
    }
}
