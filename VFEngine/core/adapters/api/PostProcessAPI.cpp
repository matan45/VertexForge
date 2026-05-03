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

    void PostProcessAPI::registerAPI(services::ScriptInterpreter* interpreter)
    {
        registerCoreEffects(interpreter);
        registerAdvancedEffects(interpreter);
        registerEdgeDetectionAndColorGrading(interpreter);
    }

    void PostProcessAPI::registerCoreEffects(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // =============================================
        // Global
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return value::Value(dispatcher.query(events::postprocess::GetPostProcessEnabledQuery{}));
            }});

        interpreter->registerNativeFunction("_native_postprocess_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                events::postprocess::SetPostProcessEnabledCommand cmd;
                cmd.enabled = extractBool(args[0]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            }});

        // =============================================
        // Tone Mapping
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.toneMapping.enabled);
            }});

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.toneMapping.enabled = extractBool(args[0]);
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_getMode",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.toneMapping.mode));
            }});

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setMode",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t mode = extractInt64(args[0], "PostProcess.toneMapping.setMode");
                    if (mode >= 0 && mode <= 6)
                    {
                        s.toneMapping.mode = static_cast<postprocess::ToneMappingMode>(mode);
                    }
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_getExposure",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.toneMapping.exposure);
            }});

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setExposure",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.toneMapping.exposure = extractFloat(args[0], "PostProcess.toneMapping.setExposure");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_getGamma",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.toneMapping.gamma);
            }});

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setGamma",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.toneMapping.gamma = extractFloat(args[0], "PostProcess.toneMapping.setGamma");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_getContrast",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.toneMapping.contrast);
            }});

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setContrast",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.toneMapping.contrast = extractFloat(args[0], "PostProcess.toneMapping.setContrast");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_getToe",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.toneMapping.toe);
            }});

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setToe",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.toneMapping.toe = extractFloat(args[0], "PostProcess.toneMapping.setToe");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_getShoulder",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.toneMapping.shoulder);
            }});

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setShoulder",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.toneMapping.shoulder = extractFloat(args[0], "PostProcess.toneMapping.setShoulder");
                });
            }});

        // =============================================
        // Bloom
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_bloom_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.bloom.enabled);
            }});

        interpreter->registerNativeFunction("_native_postprocess_bloom_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.bloom.enabled = extractBool(args[0]);
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_bloom_getThreshold",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.bloom.threshold);
            }});

        interpreter->registerNativeFunction("_native_postprocess_bloom_setThreshold",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.bloom.threshold = extractFloat(args[0], "PostProcess.bloom.setThreshold");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_bloom_getIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.bloom.intensity);
            }});

        interpreter->registerNativeFunction("_native_postprocess_bloom_setIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.bloom.intensity = extractFloat(args[0], "PostProcess.bloom.setIntensity");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_bloom_getRadius",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.bloom.radius);
            }});

        interpreter->registerNativeFunction("_native_postprocess_bloom_setRadius",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.bloom.radius = extractFloat(args[0], "PostProcess.bloom.setRadius");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_bloom_getPasses",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.bloom.passes));
            }});

        interpreter->registerNativeFunction("_native_postprocess_bloom_setPasses",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t passes = extractInt64(args[0], "PostProcess.bloom.setPasses");
                    if (passes > 0)
                    {
                        s.bloom.passes = static_cast<uint32_t>(passes);
                    }
                });
            }});

        // =============================================
        // Vignette
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_vignette_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.vignette.enabled);
            }});

        interpreter->registerNativeFunction("_native_postprocess_vignette_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.vignette.enabled = extractBool(args[0]);
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_vignette_getIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.vignette.intensity);
            }});

        interpreter->registerNativeFunction("_native_postprocess_vignette_setIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.vignette.intensity = extractFloat(args[0], "PostProcess.vignette.setIntensity");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_vignette_getRadius",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.vignette.radius);
            }});

        interpreter->registerNativeFunction("_native_postprocess_vignette_setRadius",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.vignette.radius = extractFloat(args[0], "PostProcess.vignette.setRadius");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_vignette_getSoftness",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.vignette.softness);
            }});

        interpreter->registerNativeFunction("_native_postprocess_vignette_setSoftness",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.vignette.softness = extractFloat(args[0], "PostProcess.vignette.setSoftness");
                });
            }});

        // =============================================
        // Chromatic Aberration
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_chromaticAberration_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.chromaticAberration.enabled);
            }});

        interpreter->registerNativeFunction("_native_postprocess_chromaticAberration_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.chromaticAberration.enabled = extractBool(args[0]);
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_chromaticAberration_getIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.chromaticAberration.intensity);
            }});

        interpreter->registerNativeFunction("_native_postprocess_chromaticAberration_setIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.chromaticAberration.intensity = extractFloat(args[0], "PostProcess.chromaticAberration.setIntensity");
                });
            }});

        // =============================================
        // Film Grain
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_filmGrain_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.filmGrain.enabled);
            }});

        interpreter->registerNativeFunction("_native_postprocess_filmGrain_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.filmGrain.enabled = extractBool(args[0]);
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_filmGrain_getIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.filmGrain.intensity);
            }});

        interpreter->registerNativeFunction("_native_postprocess_filmGrain_setIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.filmGrain.intensity = extractFloat(args[0], "PostProcess.filmGrain.setIntensity");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_filmGrain_getSize",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.filmGrain.size);
            }});

        interpreter->registerNativeFunction("_native_postprocess_filmGrain_setSize",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.filmGrain.size = extractFloat(args[0], "PostProcess.filmGrain.setSize");
                });
            }});
    }
}
