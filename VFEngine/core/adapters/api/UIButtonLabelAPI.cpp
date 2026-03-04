// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "UIButtonLabelAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIEvents.hpp"

namespace core::api
{
    namespace
    {
        value::Value getButtonData(events::EventDispatcher& dispatcher,
                                   const std::vector<value::Value>& args,
                                   const char* context)
        {
            if (args.empty()) return value::Value(static_cast<int64_t>(-1));
            auto handle = intToEntity(extractInt64(args[0], context));

            events::ui::HasUIButtonComponentQuery hasQuery;
            hasQuery.entity = handle;
            if (!dispatcher.query(hasQuery))
                return value::Value(static_cast<int64_t>(-1));

            events::ui::GetUIButtonDataQuery getQuery;
            getQuery.entity = handle;
            auto data = dispatcher.query(getQuery);
            if (!data.has_value())
                return value::Value(static_cast<int64_t>(-1));

            return value::Value(static_cast<int64_t>(data->currentState));
        }
    }

    void UIButtonLabelAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        interpreter->registerNativeFunction("_native_ui_isButtonHovered",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                auto state = getButtonData(dispatcher, args, "_native_ui_isButtonHovered");
                if (std::holds_alternative<int64_t>(state))
                    return value::Value(std::get<int64_t>(state) == 1);
                return value::Value(false);
            });

        interpreter->registerNativeFunction("_native_ui_isButtonPressed",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                auto state = getButtonData(dispatcher, args, "_native_ui_isButtonPressed");
                if (std::holds_alternative<int64_t>(state))
                    return value::Value(std::get<int64_t>(state) == 2);
                return value::Value(false);
            });

        interpreter->registerNativeFunction("_native_ui_getButtonState",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return getButtonData(dispatcher, args, "_native_ui_getButtonState");
            });

        interpreter->registerNativeFunction("_native_ui_setButtonInteractable",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setButtonInteractable"));
                bool interactable = extractBool(args[1]);

                events::ui::GetUIButtonDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto buttonData = data.value();
                buttonData.interactable = interactable;

                events::ui::SetUIButtonDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.buttonData = buttonData;
                dispatcher.execute(setCmd);
                return value::Value();
            });

        interpreter->registerNativeFunction("_native_ui_getLabelText",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty()) return value::Value(std::string(""));
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_getLabelText"));

                events::ui::HasUILabelComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery)) return value::Value(std::string(""));

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value(std::string(""));

                return value::Value(data->text);
            });

        interpreter->registerNativeFunction("_native_ui_setLabelText",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2) return value::Value();
                auto handle = intToEntity(extractInt64(args[0], "_native_ui_setLabelText"));
                std::string text = extractString(args[1], "_native_ui_setLabelText");

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value()) return value::Value();

                auto labelData = data.value();
                labelData.text = text;

                events::ui::SetUILabelDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.labelData = labelData;
                dispatcher.execute(setCmd);
                return value::Value();
            });

        vfLogInfo("[UIButtonLabelAPI] Registered Button/Label native functions");
    }
}
