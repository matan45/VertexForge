// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "UIAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/UIEvents.hpp"

namespace core::api
{
    void UIAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // _native_ui_isButtonHovered(entityId) -> bool
        interpreter->registerNativeFunction("_native_ui_isButtonHovered",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(false);
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_isButtonHovered");
                auto handle = intToEntity(entityId);

                events::ui::HasUIButtonComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery))
                {
                    return value::Value(false);
                }

                events::ui::GetUIButtonDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value(false);
                }

                return value::Value(data->currentState == 1); // Hovered
            });

        // _native_ui_isButtonPressed(entityId) -> bool
        interpreter->registerNativeFunction("_native_ui_isButtonPressed",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(false);
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_isButtonPressed");
                auto handle = intToEntity(entityId);

                events::ui::HasUIButtonComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery))
                {
                    return value::Value(false);
                }

                events::ui::GetUIButtonDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value(false);
                }

                return value::Value(data->currentState == 2); // Pressed
            });

        // _native_ui_getButtonState(entityId) -> int (0=Normal, 1=Hovered, 2=Pressed, 3=Disabled)
        interpreter->registerNativeFunction("_native_ui_getButtonState",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(static_cast<int64_t>(-1));
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_getButtonState");
                auto handle = intToEntity(entityId);

                events::ui::HasUIButtonComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery))
                {
                    return value::Value(static_cast<int64_t>(-1));
                }

                events::ui::GetUIButtonDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value(static_cast<int64_t>(-1));
                }

                return value::Value(static_cast<int64_t>(data->currentState));
            });

        // _native_ui_setButtonInteractable(entityId, interactable) -> void
        interpreter->registerNativeFunction("_native_ui_setButtonInteractable",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value();
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_setButtonInteractable");
                bool interactable = std::holds_alternative<bool>(args[1]) ? std::get<bool>(args[1]) : true;
                auto handle = intToEntity(entityId);

                events::ui::GetUIButtonDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value();
                }

                auto buttonData = data.value();
                buttonData.interactable = interactable;

                events::ui::SetUIButtonDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.buttonData = buttonData;
                dispatcher.execute(setCmd);

                return value::Value();
            });

        // _native_ui_getLabelText(entityId) -> String
        interpreter->registerNativeFunction("_native_ui_getLabelText",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(std::string(""));
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_getLabelText");
                auto handle = intToEntity(entityId);

                events::ui::HasUILabelComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery))
                {
                    return value::Value(std::string(""));
                }

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value(std::string(""));
                }

                return value::Value(data->text);
            });

        // _native_ui_setLabelText(entityId, text) -> void
        interpreter->registerNativeFunction("_native_ui_setLabelText",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value();
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_setLabelText");
                std::string text = extractString(args[1], "_native_ui_setLabelText");
                auto handle = intToEntity(entityId);

                events::ui::GetUILabelDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value();
                }

                auto labelData = data.value();
                labelData.text = text;

                events::ui::SetUILabelDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.labelData = labelData;
                dispatcher.execute(setCmd);

                return value::Value();
            });

        // ============================================
        // UI Text Input
        // ============================================

        // _native_ui_getTextInputText(entityId) -> String
        interpreter->registerNativeFunction("_native_ui_getTextInputText",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(std::string(""));
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_getTextInputText");
                auto handle = intToEntity(entityId);

                events::ui::HasUITextInputComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery))
                {
                    return value::Value(std::string(""));
                }

                events::ui::GetUITextInputDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value(std::string(""));
                }

                return value::Value(data->text);
            });

        // _native_ui_setTextInputText(entityId, text) -> void
        interpreter->registerNativeFunction("_native_ui_setTextInputText",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value();
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_setTextInputText");
                std::string text = extractString(args[1], "_native_ui_setTextInputText");
                auto handle = intToEntity(entityId);

                events::ui::GetUITextInputDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value();
                }

                auto textInputData = data.value();
                textInputData.text = text;

                events::ui::SetUITextInputDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.textInputData = textInputData;
                dispatcher.execute(setCmd);

                return value::Value();
            });

        // _native_ui_getTextInputState(entityId) -> int (0=Normal, 1=Hovered, 2=Focused, 3=Disabled)
        interpreter->registerNativeFunction("_native_ui_getTextInputState",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(static_cast<int64_t>(-1));
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_getTextInputState");
                auto handle = intToEntity(entityId);

                events::ui::HasUITextInputComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery))
                {
                    return value::Value(static_cast<int64_t>(-1));
                }

                events::ui::GetUITextInputDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value(static_cast<int64_t>(-1));
                }

                return value::Value(static_cast<int64_t>(data->currentState));
            });

        // _native_ui_setTextInputInteractable(entityId, interactable) -> void
        interpreter->registerNativeFunction("_native_ui_setTextInputInteractable",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value();
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_setTextInputInteractable");
                bool interactable = std::holds_alternative<bool>(args[1]) ? std::get<bool>(args[1]) : true;
                auto handle = intToEntity(entityId);

                events::ui::GetUITextInputDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value();
                }

                auto textInputData = data.value();
                textInputData.interactable = interactable;

                events::ui::SetUITextInputDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.textInputData = textInputData;
                dispatcher.execute(setCmd);

                return value::Value();
            });
    }
}
