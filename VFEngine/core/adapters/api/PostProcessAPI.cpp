// mType headers must come first to avoid Windows macro conflicts
#include <services/ScriptInterpreter.hpp>

#include "PostProcessAPI.hpp"
#include "NativeHelpers.hpp"
#include "../../../services/events/EventDispatcher.hpp"
#include "../../../services/events/PostProcessEvents.hpp"

namespace core::api
{
    namespace
    {
        template<typename Mutator>
        value::Value modifySettings(events::EventDispatcher& dispatcher, Mutator&& mutator)
        {
            auto settings = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
            mutator(settings);
            events::postprocess::ApplyPostProcessSettingsCommand cmd;
            cmd.settings = settings;
            dispatcher.execute(cmd);
            return value::Value(std::monostate{});
        }
    }

    void PostProcessAPI::registerAPI(services::ScriptInterpreter* interpreter)
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

        // =============================================
        // FXAA
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_fxaa_isEnabled",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.fxaa.enabled);
            });

        interpreter->registerNativeFunction("_native_postprocess_fxaa_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.fxaa.enabled = extractBool(args[0]);
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_fxaa_getQuality",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.fxaa.quality));
            });

        interpreter->registerNativeFunction("_native_postprocess_fxaa_setQuality",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t quality = extractInt64(args[0], "PostProcess.fxaa.setQuality");
                    if (quality >= 0 && quality <= 2)
                    {
                        s.fxaa.quality = static_cast<postprocess::FXAAQuality>(quality);
                    }
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_fxaa_getEdgeThresholdMin",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.fxaa.edgeThresholdMin);
            });

        interpreter->registerNativeFunction("_native_postprocess_fxaa_setEdgeThresholdMin",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.fxaa.edgeThresholdMin = extractFloat(args[0], "PostProcess.fxaa.setEdgeThresholdMin");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_fxaa_getEdgeThreshold",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.fxaa.edgeThreshold);
            });

        interpreter->registerNativeFunction("_native_postprocess_fxaa_setEdgeThreshold",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.fxaa.edgeThreshold = extractFloat(args[0], "PostProcess.fxaa.setEdgeThreshold");
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

        // =============================================
        // Depth of Field
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_dof_isEnabled",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.enabled);
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.depthOfField.enabled = extractBool(args[0]);
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocusMode",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.depthOfField.focusMode));
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_setFocusMode",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t mode = extractInt64(args[0], "PostProcess.dof.setFocusMode");
                    if (mode >= 0 && mode <= 1)
                    {
                        s.depthOfField.focusMode = static_cast<postprocess::DoFFocusMode>(mode);
                    }
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocalDistance",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.focalDistance);
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_setFocalDistance",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.depthOfField.focalDistance = extractFloat(args[0], "PostProcess.dof.setFocalDistance");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocusTargetX",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.focusTargetX);
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocusTargetY",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.focusTargetY);
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocusTargetZ",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.focusTargetZ);
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_setFocusTarget",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.depthOfField.focusTargetX = extractFloat(args[0], "PostProcess.dof.setFocusTarget x");
                    s.depthOfField.focusTargetY = extractFloat(args[1], "PostProcess.dof.setFocusTarget y");
                    s.depthOfField.focusTargetZ = extractFloat(args[2], "PostProcess.dof.setFocusTarget z");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocusSmoothing",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.focusSmoothing);
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_setFocusSmoothing",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.depthOfField.focusSmoothing = extractFloat(args[0], "PostProcess.dof.setFocusSmoothing");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocalRange",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.focalRange);
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_setFocalRange",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.depthOfField.focalRange = extractFloat(args[0], "PostProcess.dof.setFocalRange");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_getMaxBlurRadius",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.maxBlurRadius);
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_setMaxBlurRadius",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.depthOfField.maxBlurRadius = extractFloat(args[0], "PostProcess.dof.setMaxBlurRadius");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_getSampleCount",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.depthOfField.sampleCount));
            });

        interpreter->registerNativeFunction("_native_postprocess_dof_setSampleCount",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t count = extractInt64(args[0], "PostProcess.dof.setSampleCount");
                    if (count >= 4 && count <= 32)
                    {
                        s.depthOfField.sampleCount = static_cast<int>(count);
                    }
                });
            });

        // =============================================
        // Volumetric Fog
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_isEnabled",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.enabled);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.enabled = extractBool(args[0]);
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getQuality",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.volumetricFog.quality));
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setQuality",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t quality = extractInt64(args[0], "PostProcess.volumetricFog.setQuality");
                    if (quality >= 0 && quality <= 2)
                    {
                        s.volumetricFog.quality = static_cast<postprocess::VolumetricQuality>(quality);
                    }
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getDensity",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.uniformDensity);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setDensity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.uniformDensity = extractFloat(args[0], "PostProcess.volumetricFog.setDensity");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getColorR",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.fogColor[0]);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getColorG",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.fogColor[1]);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getColorB",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.fogColor[2]);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setColor",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.fogColor[0] = extractFloat(args[0], "PostProcess.volumetricFog.setColor r");
                    s.volumetricFog.fogColor[1] = extractFloat(args[1], "PostProcess.volumetricFog.setColor g");
                    s.volumetricFog.fogColor[2] = extractFloat(args[2], "PostProcess.volumetricFog.setColor b");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getHeightFogDensity",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.heightFogDensity);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setHeightFogDensity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.heightFogDensity = extractFloat(args[0], "PostProcess.volumetricFog.setHeightFogDensity");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getHeightFogFalloff",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.heightFogFalloff);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setHeightFogFalloff",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.heightFogFalloff = extractFloat(args[0], "PostProcess.volumetricFog.setHeightFogFalloff");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getHeightFogOffset",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.heightFogOffset);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setHeightFogOffset",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.heightFogOffset = extractFloat(args[0], "PostProcess.volumetricFog.setHeightFogOffset");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getScatteringCoefficient",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.scatteringCoefficient);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setScatteringCoefficient",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.scatteringCoefficient = extractFloat(args[0], "PostProcess.volumetricFog.setScatteringCoefficient");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getAbsorptionCoefficient",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.absorptionCoefficient);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setAbsorptionCoefficient",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.absorptionCoefficient = extractFloat(args[0], "PostProcess.volumetricFog.setAbsorptionCoefficient");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getAnisotropy",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.anisotropy);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setAnisotropy",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.anisotropy = extractFloat(args[0], "PostProcess.volumetricFog.setAnisotropy");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getIntensity",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.intensity);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setIntensity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.intensity = extractFloat(args[0], "PostProcess.volumetricFog.setIntensity");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getAmbientIntensity",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.ambientIntensity);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setAmbientIntensity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.ambientIntensity = extractFloat(args[0], "PostProcess.volumetricFog.setAmbientIntensity");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getMaxDistance",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.maxDistance);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setMaxDistance",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.maxDistance = extractFloat(args[0], "PostProcess.volumetricFog.setMaxDistance");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getTemporalBlendFactor",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.temporalBlendFactor);
            });

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setTemporalBlendFactor",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.temporalBlendFactor = extractFloat(args[0], "PostProcess.volumetricFog.setTemporalBlendFactor");
                });
            });
    }
}
