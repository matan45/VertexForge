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

    void PostProcessAPI::registerAdvancedEffects(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

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

        // =============================================
        // SSAO
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_ssao_isEnabled",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.ssao.enabled);
            });

        interpreter->registerNativeFunction("_native_postprocess_ssao_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.ssao.enabled = extractBool(args[0]);
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_ssao_getRadius",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.ssao.radius);
            });

        interpreter->registerNativeFunction("_native_postprocess_ssao_setRadius",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.ssao.radius = extractFloat(args[0], "PostProcess.ssao.setRadius");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_ssao_getBias",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.ssao.bias);
            });

        interpreter->registerNativeFunction("_native_postprocess_ssao_setBias",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.ssao.bias = extractFloat(args[0], "PostProcess.ssao.setBias");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_ssao_getIntensity",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.ssao.intensity);
            });

        interpreter->registerNativeFunction("_native_postprocess_ssao_setIntensity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.ssao.intensity = extractFloat(args[0], "PostProcess.ssao.setIntensity");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_ssao_getKernelSize",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.ssao.kernelSize));
            });

        interpreter->registerNativeFunction("_native_postprocess_ssao_setKernelSize",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t size = extractInt64(args[0], "PostProcess.ssao.setKernelSize");
                    if (size >= 8 && size <= 64)
                    {
                        s.ssao.kernelSize = static_cast<int>(size);
                    }
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_ssao_getPower",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.ssao.power);
            });

        interpreter->registerNativeFunction("_native_postprocess_ssao_setPower",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.ssao.power = extractFloat(args[0], "PostProcess.ssao.setPower");
                });
            });

        // =============================================
        // Edge Detection
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_isEnabled",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.enabled);
            });

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_setEnabled",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.edgeDetection.enabled = extractBool(args[0]);
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_getThreshold",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.threshold);
            });

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_setThreshold",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.edgeDetection.threshold = extractFloat(args[0], "PostProcess.edgeDetection.setThreshold");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_getEdgeWidth",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.edgeWidth);
            });

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_setEdgeWidth",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.edgeDetection.edgeWidth = extractFloat(args[0], "PostProcess.edgeDetection.setEdgeWidth");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_getColorR",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.edgeColor[0]);
            });

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_getColorG",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.edgeColor[1]);
            });

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_getColorB",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.edgeColor[2]);
            });

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_setColor",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.edgeDetection.edgeColor[0] = extractFloat(args[0], "PostProcess.edgeDetection.setColor r");
                    s.edgeDetection.edgeColor[1] = extractFloat(args[1], "PostProcess.edgeDetection.setColor g");
                    s.edgeDetection.edgeColor[2] = extractFloat(args[2], "PostProcess.edgeDetection.setColor b");
                });
            });

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_getOpacity",
            [&dispatcher](const std::vector<value::Value>&) -> value::Value
            {
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.edgeDetection.opacity);
            });

        interpreter->registerNativeFunction("_native_postprocess_edgeDetection_setOpacity",
            [&dispatcher](const std::vector<value::Value>& args) -> value::Value
            {
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.edgeDetection.opacity = extractFloat(args[0], "PostProcess.edgeDetection.setOpacity");
                });
            });
    }
}
