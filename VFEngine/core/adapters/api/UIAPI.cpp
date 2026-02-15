// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "UIAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/UIEvents.hpp"
#include "../../../services/events/SceneEvents.hpp"

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

        // ============================================
        // UI Checkbox
        // ============================================

        // _native_ui_isCheckboxChecked(entityId) -> bool
        interpreter->registerNativeFunction("_native_ui_isCheckboxChecked",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(false);
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_isCheckboxChecked");
                auto handle = intToEntity(entityId);

                events::ui::HasUICheckboxComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery))
                {
                    return value::Value(false);
                }

                events::ui::GetUICheckboxDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value(false);
                }

                return value::Value(data->isChecked);
            });

        // _native_ui_setCheckboxChecked(entityId, checked) -> void
        interpreter->registerNativeFunction("_native_ui_setCheckboxChecked",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value();
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_setCheckboxChecked");
                bool checked = std::holds_alternative<bool>(args[1]) ? std::get<bool>(args[1]) : false;
                auto handle = intToEntity(entityId);

                events::ui::GetUICheckboxDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value();
                }

                auto checkboxData = data.value();
                checkboxData.isChecked = checked;

                events::ui::SetUICheckboxDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.checkboxData = checkboxData;
                dispatcher.execute(setCmd);

                return value::Value();
            });

        // _native_ui_getCheckboxState(entityId) -> int (0=Normal, 1=Hovered, 2=Disabled)
        interpreter->registerNativeFunction("_native_ui_getCheckboxState",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(static_cast<int64_t>(-1));
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_getCheckboxState");
                auto handle = intToEntity(entityId);

                events::ui::HasUICheckboxComponentQuery hasQuery;
                hasQuery.entity = handle;
                if (!dispatcher.query(hasQuery))
                {
                    return value::Value(static_cast<int64_t>(-1));
                }

                events::ui::GetUICheckboxDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value(static_cast<int64_t>(-1));
                }

                return value::Value(static_cast<int64_t>(data->currentState));
            });

        // _native_ui_setCheckboxInteractable(entityId, interactable) -> void
        interpreter->registerNativeFunction("_native_ui_setCheckboxInteractable",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value();
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_setCheckboxInteractable");
                bool interactable = std::holds_alternative<bool>(args[1]) ? std::get<bool>(args[1]) : true;
                auto handle = intToEntity(entityId);

                events::ui::GetUICheckboxDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value();
                }

                auto checkboxData = data.value();
                checkboxData.interactable = interactable;

                events::ui::SetUICheckboxDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.checkboxData = checkboxData;
                dispatcher.execute(setCmd);

                return value::Value();
            });

        // _native_ui_getCheckboxLabelText(entityId) -> String
        // Walks children of the checkbox entity to find a UILabel and returns its text
        interpreter->registerNativeFunction("_native_ui_getCheckboxLabelText",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(std::string(""));
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_getCheckboxLabelText");
                auto handle = intToEntity(entityId);

                // Get entity data to access children
                events::scene::GetEntityQuery entityQuery;
                entityQuery.entity = handle;
                auto entityData = dispatcher.query(entityQuery);
                if (!entityData.has_value())
                {
                    return value::Value(std::string(""));
                }

                // Walk children looking for one with UILabel
                for (const auto& childHandle : entityData->children)
                {
                    events::ui::HasUILabelComponentQuery hasLabelQuery;
                    hasLabelQuery.entity = childHandle;
                    if (dispatcher.query(hasLabelQuery))
                    {
                        events::ui::GetUILabelDataQuery getLabelQuery;
                        getLabelQuery.entity = childHandle;
                        auto labelData = dispatcher.query(getLabelQuery);
                        if (labelData.has_value())
                        {
                            return value::Value(labelData->text);
                        }
                    }
                }

                return value::Value(std::string(""));
            });

        // ============================================
        // UI Dropdown
        // ============================================

        // _native_ui_getDropdownSelectedIndex(entityId) -> int
        interpreter->registerNativeFunction("_native_ui_getDropdownSelectedIndex",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(static_cast<int64_t>(-1));
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_getDropdownSelectedIndex");
                auto handle = intToEntity(entityId);

                events::ui::GetUIDropdownDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value(static_cast<int64_t>(-1));
                }

                return value::Value(static_cast<int64_t>(data->selectedIndex));
            });

        // _native_ui_setDropdownSelectedIndex(entityId, index) -> void
        interpreter->registerNativeFunction("_native_ui_setDropdownSelectedIndex",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value();
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_setDropdownSelectedIndex");
                int64_t index = extractInt64(args[1], "_native_ui_setDropdownSelectedIndex");
                auto handle = intToEntity(entityId);

                events::ui::SetUIDropdownSelectedIndexCommand cmd;
                cmd.entity = handle;
                cmd.selectedIndex = static_cast<int>(index);
                dispatcher.execute(cmd);

                return value::Value();
            });

        // _native_ui_getDropdownSelectedValue(entityId) -> String
        interpreter->registerNativeFunction("_native_ui_getDropdownSelectedValue",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(std::string(""));
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_getDropdownSelectedValue");
                auto handle = intToEntity(entityId);

                events::ui::GetUIDropdownDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value(std::string(""));
                }

                if (data->selectedIndex >= 0 && data->selectedIndex < static_cast<int>(data->options.size()))
                {
                    return value::Value(data->options[data->selectedIndex].text);
                }

                return value::Value(std::string(""));
            });

        // _native_ui_getDropdownState(entityId) -> int (0=Normal, 1=Hovered, 2=Open, 3=Disabled)
        interpreter->registerNativeFunction("_native_ui_getDropdownState",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value(static_cast<int64_t>(-1));
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_getDropdownState");
                auto handle = intToEntity(entityId);

                events::ui::GetUIDropdownDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value(static_cast<int64_t>(-1));
                }

                return value::Value(static_cast<int64_t>(data->currentState));
            });

        // _native_ui_setDropdownInteractable(entityId, interactable) -> void
        interpreter->registerNativeFunction("_native_ui_setDropdownInteractable",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value();
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_setDropdownInteractable");
                bool interactable = std::holds_alternative<bool>(args[1]) ? std::get<bool>(args[1]) : true;
                auto handle = intToEntity(entityId);

                events::ui::GetUIDropdownDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value();
                }

                auto dropdownData = data.value();
                dropdownData.interactable = interactable;

                events::ui::SetUIDropdownDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.dropdownData = dropdownData;
                dispatcher.execute(setCmd);

                return value::Value();
            });

        // _native_ui_setDropdownOptions(entityId, optionsStr) -> void (pipe-delimited)
        interpreter->registerNativeFunction("_native_ui_setDropdownOptions",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.size() < 2)
                {
                    return value::Value();
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_setDropdownOptions");
                std::string optionsStr = extractString(args[1], "_native_ui_setDropdownOptions");
                auto handle = intToEntity(entityId);

                events::ui::GetUIDropdownDataQuery getQuery;
                getQuery.entity = handle;
                auto data = dispatcher.query(getQuery);
                if (!data.has_value())
                {
                    return value::Value();
                }

                auto dropdownData = data.value();
                dropdownData.options.clear();

                // Split by pipe delimiter
                std::string::size_type start = 0;
                std::string::size_type pos = optionsStr.find('|');
                while (pos != std::string::npos)
                {
                    dropdownData.options.push_back({optionsStr.substr(start, pos - start), ""});
                    start = pos + 1;
                    pos = optionsStr.find('|', start);
                }
                if (start < optionsStr.size())
                {
                    dropdownData.options.push_back({optionsStr.substr(start), ""});
                }

                // Clamp selectedIndex
                if (dropdownData.selectedIndex >= static_cast<int>(dropdownData.options.size()))
                {
                    dropdownData.selectedIndex = static_cast<int>(dropdownData.options.size()) - 1;
                }

                events::ui::SetUIDropdownDataCommand setCmd;
                setCmd.entity = handle;
                setCmd.dropdownData = dropdownData;
                dispatcher.execute(setCmd);

                return value::Value();
            });

        // _native_ui_openDropdown(entityId) -> void
        interpreter->registerNativeFunction("_native_ui_openDropdown",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value();
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_openDropdown");
                auto handle = intToEntity(entityId);

                events::ui::OpenUIDropdownCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);

                return value::Value();
            });

        // _native_ui_closeDropdown(entityId) -> void
        interpreter->registerNativeFunction("_native_ui_closeDropdown",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                if (args.empty())
                {
                    return value::Value();
                }
                int64_t entityId = extractInt64(args[0], "_native_ui_closeDropdown");
                auto handle = intToEntity(entityId);

                events::ui::CloseUIDropdownCommand cmd;
                cmd.entity = handle;
                dispatcher.execute(cmd);

                return value::Value();
            });
    }
}
