// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "UIInputAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIEvents.hpp"
#include "../../../services/events/project/SceneEvents.hpp"

namespace core::api
{
    namespace
    {
        void registerTextInputFunctions(services::ScriptInterpreter* interpreter,
                                        events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_ui_getTextInputText",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(std::string(""));
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getTextInputText"));

                    events::ui::HasUITextInputComponentQuery hasQuery;
                    hasQuery.entity = handle;
                    if (!dispatcher.query(hasQuery)) return value::Value(std::string(""));

                    events::ui::GetUITextInputDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(std::string(""));

                    return value::Value(data->text);
                }});

            interpreter->registerNativeFunction("_native_ui_setTextInputText",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value();
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_setTextInputText"));
                    std::string text = extractString(args[1], "_native_ui_setTextInputText");

                    events::ui::GetUITextInputDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value();

                    auto textInputData = data.value();
                    textInputData.text = text;

                    events::ui::SetUITextInputDataCommand setCmd;
                    setCmd.entity = handle;
                    setCmd.textInputData = textInputData;
                    dispatcher.execute(setCmd);
                    return value::Value();
                }});

            interpreter->registerNativeFunction("_native_ui_getTextInputState",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(static_cast<int64_t>(-1));
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getTextInputState"));

                    events::ui::HasUITextInputComponentQuery hasQuery;
                    hasQuery.entity = handle;
                    if (!dispatcher.query(hasQuery)) return value::Value(static_cast<int64_t>(-1));

                    events::ui::GetUITextInputDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(static_cast<int64_t>(-1));

                    return value::Value(static_cast<int64_t>(data->currentState));
                }});

            interpreter->registerNativeFunction("_native_ui_setTextInputInteractable",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value();
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_setTextInputInteractable"));
                    bool interactable = extractBool(args[1]);

                    events::ui::GetUITextInputDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value();

                    auto textInputData = data.value();
                    textInputData.interactable = interactable;

                    events::ui::SetUITextInputDataCommand setCmd;
                    setCmd.entity = handle;
                    setCmd.textInputData = textInputData;
                    dispatcher.execute(setCmd);
                    return value::Value();
                }});
        }

        void registerCheckboxFunctions(services::ScriptInterpreter* interpreter,
                                       events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_ui_isCheckboxChecked",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(false);
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_isCheckboxChecked"));

                    events::ui::HasUICheckboxComponentQuery hasQuery;
                    hasQuery.entity = handle;
                    if (!dispatcher.query(hasQuery)) return value::Value(false);

                    events::ui::GetUICheckboxDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(false);

                    return value::Value(data->isChecked);
                }});

            interpreter->registerNativeFunction("_native_ui_setCheckboxChecked",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value();
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_setCheckboxChecked"));
                    bool checked = extractBool(args[1]);

                    events::ui::GetUICheckboxDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value();

                    auto checkboxData = data.value();
                    checkboxData.isChecked = checked;

                    events::ui::SetUICheckboxDataCommand setCmd;
                    setCmd.entity = handle;
                    setCmd.checkboxData = checkboxData;
                    dispatcher.execute(setCmd);
                    return value::Value();
                }});

            interpreter->registerNativeFunction("_native_ui_getCheckboxState",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(static_cast<int64_t>(-1));
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getCheckboxState"));

                    events::ui::HasUICheckboxComponentQuery hasQuery;
                    hasQuery.entity = handle;
                    if (!dispatcher.query(hasQuery)) return value::Value(static_cast<int64_t>(-1));

                    events::ui::GetUICheckboxDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(static_cast<int64_t>(-1));

                    return value::Value(static_cast<int64_t>(data->currentState));
                }});

            interpreter->registerNativeFunction("_native_ui_setCheckboxInteractable",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value();
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_setCheckboxInteractable"));
                    bool interactable = extractBool(args[1]);

                    events::ui::GetUICheckboxDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value();

                    auto checkboxData = data.value();
                    checkboxData.interactable = interactable;

                    events::ui::SetUICheckboxDataCommand setCmd;
                    setCmd.entity = handle;
                    setCmd.checkboxData = checkboxData;
                    dispatcher.execute(setCmd);
                    return value::Value();
                }});

            interpreter->registerNativeFunction("_native_ui_getCheckboxLabelText",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(std::string(""));
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getCheckboxLabelText"));

                    events::scene::GetEntityQuery entityQuery;
                    entityQuery.entity = handle;
                    auto entityData = dispatcher.query(entityQuery);
                    if (!entityData.has_value()) return value::Value(std::string(""));

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
                                return value::Value(labelData->text);
                        }
                    }
                    return value::Value(std::string(""));
                }});
        }

        void registerDropdownFunctions(services::ScriptInterpreter* interpreter,
                                       events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_ui_getDropdownSelectedIndex",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(static_cast<int64_t>(-1));
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getDropdownSelectedIndex"));

                    events::ui::GetUIDropdownDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(static_cast<int64_t>(-1));

                    return value::Value(static_cast<int64_t>(data->selectedIndex));
                }});

            interpreter->registerNativeFunction("_native_ui_setDropdownSelectedIndex",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value();
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_setDropdownSelectedIndex"));
                    int64_t index = extractInt64(args[1], "_native_ui_setDropdownSelectedIndex");

                    events::ui::SetUIDropdownSelectedIndexCommand cmd;
                    cmd.entity = handle;
                    cmd.selectedIndex = static_cast<int>(index);
                    dispatcher.execute(cmd);
                    return value::Value();
                }});

            interpreter->registerNativeFunction("_native_ui_getDropdownSelectedValue",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(std::string(""));
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getDropdownSelectedValue"));

                    events::ui::GetUIDropdownDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(std::string(""));

                    if (data->selectedIndex >= 0 &&
                        data->selectedIndex < static_cast<int>(data->options.size()))
                        return value::Value(data->options[data->selectedIndex].text);

                    return value::Value(std::string(""));
                }});

            interpreter->registerNativeFunction("_native_ui_getDropdownState",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(static_cast<int64_t>(-1));
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getDropdownState"));

                    events::ui::GetUIDropdownDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(static_cast<int64_t>(-1));

                    return value::Value(static_cast<int64_t>(data->currentState));
                }});

            interpreter->registerNativeFunction("_native_ui_setDropdownInteractable",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value();
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_setDropdownInteractable"));
                    bool interactable = extractBool(args[1]);

                    events::ui::GetUIDropdownDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value();

                    auto dropdownData = data.value();
                    dropdownData.interactable = interactable;

                    events::ui::SetUIDropdownDataCommand setCmd;
                    setCmd.entity = handle;
                    setCmd.dropdownData = dropdownData;
                    dispatcher.execute(setCmd);
                    return value::Value();
                }});

            interpreter->registerNativeFunction("_native_ui_setDropdownOptions",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value();
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_setDropdownOptions"));
                    std::string optionsStr = extractString(args[1], "_native_ui_setDropdownOptions");

                    events::ui::GetUIDropdownDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value();

                    auto dropdownData = data.value();
                    dropdownData.options.clear();

                    std::string::size_type start = 0;
                    std::string::size_type pos = optionsStr.find('|');
                    while (pos != std::string::npos)
                    {
                        dropdownData.options.push_back({optionsStr.substr(start, pos - start), asset::AssetRef{}});
                        start = pos + 1;
                        pos = optionsStr.find('|', start);
                    }
                    if (start < optionsStr.size())
                        dropdownData.options.push_back({optionsStr.substr(start), asset::AssetRef{}});

                    if (dropdownData.selectedIndex >= static_cast<int>(dropdownData.options.size()))
                        dropdownData.selectedIndex = static_cast<int>(dropdownData.options.size()) - 1;

                    events::ui::SetUIDropdownDataCommand setCmd;
                    setCmd.entity = handle;
                    setCmd.dropdownData = dropdownData;
                    dispatcher.execute(setCmd);
                    return value::Value();
                }});

            interpreter->registerNativeFunction("_native_ui_openDropdown",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value();
                    events::ui::OpenUIDropdownCommand cmd;
                    cmd.entity = intToEntity(extractInt64(args[0], "_native_ui_openDropdown"));
                    dispatcher.execute(cmd);
                    return value::Value();
                }});

            interpreter->registerNativeFunction("_native_ui_closeDropdown",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value();
                    events::ui::CloseUIDropdownCommand cmd;
                    cmd.entity = intToEntity(extractInt64(args[0], "_native_ui_closeDropdown"));
                    dispatcher.execute(cmd);
                    return value::Value();
                }});
        }
    }

    void UIInputAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        registerTextInputFunctions(interpreter, dispatcher);
        registerCheckboxFunctions(interpreter, dispatcher);
        registerDropdownFunctions(interpreter, dispatcher);

        vfLogInfo("[UIInputAPI] Registered TextInput/Checkbox/Dropdown native functions");
    }
}
