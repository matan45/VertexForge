// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "UIValueAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/ui/UIEvents.hpp"

namespace core::api
{
    namespace
    {
        void registerTabsFunctions(services::ScriptInterpreter* interpreter,
                                   events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_ui_getTabsActiveIndex",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(static_cast<int64_t>(-1));
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getTabsActiveIndex"));

                    events::ui::GetUITabsDataQuery query;
                    query.entity = handle;
                    auto data = dispatcher.query(query);
                    if (!data.has_value()) return value::Value(static_cast<int64_t>(-1));

                    return value::Value(static_cast<int64_t>(data->activeTabIndex));
                }});

            interpreter->registerNativeFunction("_native_ui_setTabsActiveIndex",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value();
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_setTabsActiveIndex"));
                    int64_t tabIndex = extractInt64(args[1], "_native_ui_setTabsActiveIndex");

                    events::ui::SetUITabsActiveTabCommand cmd;
                    cmd.entity = handle;
                    cmd.tabIndex = static_cast<int>(tabIndex);
                    dispatcher.execute(cmd);
                    return value::Value();
                }});

            interpreter->registerNativeFunction("_native_ui_getTabsBarPosition",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(static_cast<int64_t>(0));
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getTabsBarPosition"));

                    events::ui::GetUITabsDataQuery query;
                    query.entity = handle;
                    auto data = dispatcher.query(query);
                    if (!data.has_value()) return value::Value(static_cast<int64_t>(0));

                    return value::Value(static_cast<int64_t>(data->tabBarPosition));
                }});
        }

        void registerSliderFunctions(services::ScriptInterpreter* interpreter,
                                     events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_ui_getSliderValue",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(0.0f);
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getSliderValue"));

                    events::ui::GetUISliderDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(0.0f);

                    return value::Value(data->value);
                }});

            interpreter->registerNativeFunction("_native_ui_setSliderValue",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value();
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_setSliderValue"));

                    events::ui::SetUISliderValueCommand cmd;
                    cmd.entity = handle;
                    cmd.value = extractFloat(args[1], "_native_ui_setSliderValue");
                    dispatcher.execute(cmd);
                    return value::Value();
                }});

            interpreter->registerNativeFunction("_native_ui_getSliderState",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(static_cast<int64_t>(-1));
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getSliderState"));

                    events::ui::GetUISliderDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(static_cast<int64_t>(-1));

                    return value::Value(static_cast<int64_t>(data->currentState));
                }});

            interpreter->registerNativeFunction("_native_ui_setSliderInteractable",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value();
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_setSliderInteractable"));
                    bool interactable = extractBool(args[1]);

                    events::ui::GetUISliderDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value();

                    auto sliderData = data.value();
                    sliderData.interactable = interactable;

                    events::ui::SetUISliderDataCommand setCmd;
                    setCmd.entity = handle;
                    setCmd.sliderData = sliderData;
                    dispatcher.execute(setCmd);
                    return value::Value();
                }});

            interpreter->registerNativeFunction("_native_ui_getSliderMin",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(0.0f);
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getSliderMin"));

                    events::ui::GetUISliderDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(0.0f);

                    return value::Value(data->minValue);
                }});

            interpreter->registerNativeFunction("_native_ui_getSliderMax",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(0.0f);
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getSliderMax"));

                    events::ui::GetUISliderDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(0.0f);

                    return value::Value(data->maxValue);
                }});

            interpreter->registerNativeFunction("_native_ui_setSliderMinMax",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 3) return value::Value();
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_setSliderMinMax"));
                    float minVal = extractFloat(args[1], "_native_ui_setSliderMinMax");
                    float maxVal = extractFloat(args[2], "_native_ui_setSliderMinMax");

                    events::ui::GetUISliderDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value();

                    auto sliderData = data.value();
                    sliderData.minValue = minVal;
                    sliderData.maxValue = maxVal;

                    events::ui::SetUISliderDataCommand setCmd;
                    setCmd.entity = handle;
                    setCmd.sliderData = sliderData;
                    dispatcher.execute(setCmd);
                    return value::Value();
                }});
        }

        void registerDragDropFunctions(services::ScriptInterpreter* interpreter,
                                       events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_ui_cancelDrag",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    events::ui::CancelDragCommand cmd;
                    bool result = dispatcher.execute(cmd);
                    return value::Value(result);
                }});
        }

        void registerProgressBarFunctions(services::ScriptInterpreter* interpreter,
                                          events::EventDispatcher& dispatcher)
        {
            interpreter->registerNativeFunction("_native_ui_getProgressBarValue",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(0.0f);
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getProgressBarValue"));

                    events::ui::GetUIProgressBarDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(0.0f);

                    return value::Value(data->value);
                }});

            interpreter->registerNativeFunction("_native_ui_setProgressBarValue",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 2) return value::Value();
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_setProgressBarValue"));

                    events::ui::SetUIProgressBarValueCommand cmd;
                    cmd.entity = handle;
                    cmd.value = extractFloat(args[1], "_native_ui_setProgressBarValue");
                    dispatcher.execute(cmd);
                    return value::Value();
                }});

            interpreter->registerNativeFunction("_native_ui_getProgressBarDisplayValue",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(0.0f);
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getProgressBarDisplayValue"));

                    events::ui::GetUIProgressBarDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(0.0f);

                    return value::Value(data->displayValue);
                }});

            interpreter->registerNativeFunction("_native_ui_getProgressBarMin",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(0.0f);
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getProgressBarMin"));

                    events::ui::GetUIProgressBarDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(0.0f);

                    return value::Value(data->minValue);
                }});

            interpreter->registerNativeFunction("_native_ui_getProgressBarMax",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(0.0f);
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_getProgressBarMax"));

                    events::ui::GetUIProgressBarDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(0.0f);

                    return value::Value(data->maxValue);
                }});

            interpreter->registerNativeFunction("_native_ui_setProgressBarMinMax",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.size() < 3) return value::Value();
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_setProgressBarMinMax"));
                    float minVal = extractFloat(args[1], "_native_ui_setProgressBarMinMax");
                    float maxVal = extractFloat(args[2], "_native_ui_setProgressBarMinMax");

                    events::ui::GetUIProgressBarDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value();

                    auto progressBarData = data.value();
                    progressBarData.minValue = minVal;
                    progressBarData.maxValue = maxVal;

                    events::ui::SetUIProgressBarDataCommand setCmd;
                    setCmd.entity = handle;
                    setCmd.progressBarData = progressBarData;
                    dispatcher.execute(setCmd);
                    return value::Value();
                }});

            interpreter->registerNativeFunction("_native_ui_isProgressBarCompleted",
                {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                    auto& dispatcher = events::EventDispatcher::instance();
                    if (args.empty()) return value::Value(false);
                    auto handle = intToEntity(extractInt64(args[0], "_native_ui_isProgressBarCompleted"));

                    events::ui::GetUIProgressBarDataQuery getQuery;
                    getQuery.entity = handle;
                    auto data = dispatcher.query(getQuery);
                    if (!data.has_value()) return value::Value(false);

                    return value::Value(data->value >= data->maxValue);
                }});
        }
    }

    void UIValueAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        registerTabsFunctions(interpreter, dispatcher);
        registerSliderFunctions(interpreter, dispatcher);
        registerProgressBarFunctions(interpreter, dispatcher);
        registerDragDropFunctions(interpreter, dispatcher);

        vfLogInfo("[UIValueAPI] Registered Tabs/Slider/ProgressBar/DragDrop native functions");
    }
}
