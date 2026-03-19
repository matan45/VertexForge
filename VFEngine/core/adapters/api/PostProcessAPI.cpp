// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

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
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                return value::Value(dispatcher.query(events::postprocess::GetPostProcessEnabledQuery{}));
            });

        interpreter->registerNativeFunction("_native_postprocess_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                events::postprocess::SetPostProcessEnabledCommand cmd;
                cmd.enabled = extractBool(args[0]);
                dispatcher.execute(cmd);
                return value::Value(std::monostate{});
            });

        // =============================================
        // Tone Mapping
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_isEnabled",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.toneMapping.enabled);
            });

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.toneMapping.enabled = extractBool(args[0]);
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_getMode",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.toneMapping.mode));
            });

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setMode",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t mode = extractInt64(args[0], "PostProcess.toneMapping.setMode");
                    if (mode >= 0 && mode <= 6)
                    {
                        s.toneMapping.mode = static_cast<postprocess::ToneMappingMode>(mode);
                    }
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_getExposure",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.toneMapping.exposure);
            });

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setExposure",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.toneMapping.exposure = extractFloat(args[0], "PostProcess.toneMapping.setExposure");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_getGamma",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.toneMapping.gamma);
            });

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setGamma",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.toneMapping.gamma = extractFloat(args[0], "PostProcess.toneMapping.setGamma");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_getContrast",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.toneMapping.contrast);
            });

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setContrast",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.toneMapping.contrast = extractFloat(args[0], "PostProcess.toneMapping.setContrast");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_getToe",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.toneMapping.toe);
            });

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setToe",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.toneMapping.toe = extractFloat(args[0], "PostProcess.toneMapping.setToe");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_getShoulder",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.toneMapping.shoulder);
            });

        interpreter->registerNativeFunction("_native_postprocess_toneMapping_setShoulder",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.toneMapping.shoulder = extractFloat(args[0], "PostProcess.toneMapping.setShoulder");
                });
            });

        // =============================================
        // TAA
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_taa_isEnabled",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.taa.enabled);
            });

        interpreter->registerNativeFunction("_native_postprocess_taa_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.taa.enabled = extractBool(args[0]);
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_taa_getBlendFactor",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.taa.blendFactor);
            });

        interpreter->registerNativeFunction("_native_postprocess_taa_setBlendFactor",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.taa.blendFactor = extractFloat(args[0], "PostProcess.taa.setBlendFactor");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_taa_getSharpenStrength",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.taa.sharpenStrength);
            });

        interpreter->registerNativeFunction("_native_postprocess_taa_setSharpenStrength",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.taa.sharpenStrength = extractFloat(args[0], "PostProcess.taa.setSharpenStrength");
                });
            });

        // =============================================
        // Bloom
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_bloom_isEnabled",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.bloom.enabled);
            });

        interpreter->registerNativeFunction("_native_postprocess_bloom_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.bloom.enabled = extractBool(args[0]);
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_bloom_getThreshold",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.bloom.threshold);
            });

        interpreter->registerNativeFunction("_native_postprocess_bloom_setThreshold",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.bloom.threshold = extractFloat(args[0], "PostProcess.bloom.setThreshold");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_bloom_getIntensity",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.bloom.intensity);
            });

        interpreter->registerNativeFunction("_native_postprocess_bloom_setIntensity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.bloom.intensity = extractFloat(args[0], "PostProcess.bloom.setIntensity");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_bloom_getRadius",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.bloom.radius);
            });

        interpreter->registerNativeFunction("_native_postprocess_bloom_setRadius",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.bloom.radius = extractFloat(args[0], "PostProcess.bloom.setRadius");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_bloom_getPasses",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.bloom.passes));
            });

        interpreter->registerNativeFunction("_native_postprocess_bloom_setPasses",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t passes = extractInt64(args[0], "PostProcess.bloom.setPasses");
                    if (passes > 0)
                    {
                        s.bloom.passes = static_cast<uint32_t>(passes);
                    }
                });
            });

        // =============================================
        // Vignette
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_vignette_isEnabled",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.vignette.enabled);
            });

        interpreter->registerNativeFunction("_native_postprocess_vignette_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.vignette.enabled = extractBool(args[0]);
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_vignette_getIntensity",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.vignette.intensity);
            });

        interpreter->registerNativeFunction("_native_postprocess_vignette_setIntensity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.vignette.intensity = extractFloat(args[0], "PostProcess.vignette.setIntensity");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_vignette_getRadius",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.vignette.radius);
            });

        interpreter->registerNativeFunction("_native_postprocess_vignette_setRadius",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.vignette.radius = extractFloat(args[0], "PostProcess.vignette.setRadius");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_vignette_getSoftness",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.vignette.softness);
            });

        interpreter->registerNativeFunction("_native_postprocess_vignette_setSoftness",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.vignette.softness = extractFloat(args[0], "PostProcess.vignette.setSoftness");
                });
            });

        // =============================================
        // Chromatic Aberration
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_chromaticAberration_isEnabled",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.chromaticAberration.enabled);
            });

        interpreter->registerNativeFunction("_native_postprocess_chromaticAberration_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.chromaticAberration.enabled = extractBool(args[0]);
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_chromaticAberration_getIntensity",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.chromaticAberration.intensity);
            });

        interpreter->registerNativeFunction("_native_postprocess_chromaticAberration_setIntensity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.chromaticAberration.intensity = extractFloat(args[0], "PostProcess.chromaticAberration.setIntensity");
                });
            });

        // =============================================
        // Film Grain
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_filmGrain_isEnabled",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.filmGrain.enabled);
            });

        interpreter->registerNativeFunction("_native_postprocess_filmGrain_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.filmGrain.enabled = extractBool(args[0]);
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_filmGrain_getIntensity",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.filmGrain.intensity);
            });

        interpreter->registerNativeFunction("_native_postprocess_filmGrain_setIntensity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.filmGrain.intensity = extractFloat(args[0], "PostProcess.filmGrain.setIntensity");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_filmGrain_getSize",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.filmGrain.size);
            });

        interpreter->registerNativeFunction("_native_postprocess_filmGrain_setSize",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.filmGrain.size = extractFloat(args[0], "PostProcess.filmGrain.setSize");
                });
            });
    }
}
