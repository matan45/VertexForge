// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>
#include <environment/NativeContext.hpp>
#include <span>

#include "PostProcessAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/render/PostProcessEvents.hpp"
#include "PostProcessAPIHelpers.hpp"

using core::api::detail::modifySettings;

namespace core::api
{
    void PostProcessAPI::registerEdgeDetectionAndColorGrading(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.enabled);
            }});

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                { s.edgeDetection.enabled = extractBool(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_getThreshold",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.threshold);
            }});

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_setThreshold",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                { s.edgeDetection.threshold = extractFloat(args[0], "PostProcess.edgeDetection.setThreshold"); });
            }});

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_getEdgeWidth",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.edgeWidth);
            }});

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_setEdgeWidth",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                { s.edgeDetection.edgeWidth = extractFloat(args[0], "PostProcess.edgeDetection.setEdgeWidth"); });
            }});

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_getColorR",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.edgeColor[0]);
            }});

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_getColorG",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.edgeColor[1]);
            }});

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_getColorB",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.edgeColor[2]);
            }});

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_setColor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.edgeDetection.edgeColor[0] = extractFloat(args[0], "PostProcess.edgeDetection.setColor r");
                    s.edgeDetection.edgeColor[1] = extractFloat(args[1], "PostProcess.edgeDetection.setColor g");
                    s.edgeDetection.edgeColor[2] = extractFloat(args[2], "PostProcess.edgeDetection.setColor b");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_getOpacity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.opacity);
            }});

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_setOpacity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                { s.edgeDetection.opacity = extractFloat(args[0], "PostProcess.edgeDetection.setOpacity"); });
            }});

        // Color Grading

        interpreter->registerNativeFunction("_native_postprocess_colorGrading_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.colorGrading.enabled);
            }});

        interpreter->registerNativeFunction("_native_postprocess_colorGrading_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                { s.colorGrading.enabled = extractBool(args[0]); });
            }});

        interpreter->registerNativeFunction("_native_postprocess_colorGrading_getSaturation",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.colorGrading.saturation);
            }});

        interpreter->registerNativeFunction("_native_postprocess_colorGrading_setSaturation",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                { s.colorGrading.saturation = extractFloat(args[0], "PostProcess.colorGrading.setSaturation"); });
            }});

        interpreter->registerNativeFunction("_native_postprocess_colorGrading_getColorTemperature",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.colorGrading.colorTemperature);
            }});

        interpreter->registerNativeFunction("_native_postprocess_colorGrading_setColorTemperature",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                { s.colorGrading.colorTemperature = extractFloat(args[0], "PostProcess.colorGrading.setColorTemperature"); });
            }});

        interpreter->registerNativeFunction("_native_postprocess_colorGrading_getColorTint",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.colorGrading.colorTint);
            }});

        interpreter->registerNativeFunction("_native_postprocess_colorGrading_setColorTint",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                { s.colorGrading.colorTint = extractFloat(args[0], "PostProcess.colorGrading.setColorTint"); });
            }});

        interpreter->registerNativeFunction("_native_postprocess_colorGrading_getLutIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.colorGrading.lutIntensity);
            }});

        interpreter->registerNativeFunction("_native_postprocess_colorGrading_setLutIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                { s.colorGrading.lutIntensity = extractFloat(args[0], "PostProcess.colorGrading.setLutIntensity"); });
            }});

        interpreter->registerNativeFunction("_native_postprocess_colorGrading_getLutBlendFactor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.colorGrading.lutBlendFactor);
            }});

        interpreter->registerNativeFunction("_native_postprocess_colorGrading_setLutBlendFactor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                { s.colorGrading.lutBlendFactor = extractFloat(args[0], "PostProcess.colorGrading.setLutBlendFactor"); });
            }});
    }
}
