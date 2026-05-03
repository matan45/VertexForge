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

    void PostProcessAPI::registerAdvancedEffects(services::ScriptInterpreter* interpreter)
    {
        auto& dispatcher = events::EventDispatcher::instance();

        // =============================================
        // Depth of Field
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_dof_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.enabled);
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.depthOfField.enabled = extractBool(args[0]);
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocusMode",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.depthOfField.focusMode));
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_setFocusMode",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t mode = extractInt64(args[0], "PostProcess.dof.setFocusMode");
                    if (mode >= 0 && mode <= 1)
                    {
                        s.depthOfField.focusMode = static_cast<postprocess::DoFFocusMode>(mode);
                    }
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocalDistance",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.focalDistance);
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_setFocalDistance",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.depthOfField.focalDistance = extractFloat(args[0], "PostProcess.dof.setFocalDistance");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocusTargetX",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.focusTargetX);
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocusTargetY",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.focusTargetY);
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocusTargetZ",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.focusTargetZ);
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_setFocusTarget",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.depthOfField.focusTargetX = extractFloat(args[0], "PostProcess.dof.setFocusTarget x");
                    s.depthOfField.focusTargetY = extractFloat(args[1], "PostProcess.dof.setFocusTarget y");
                    s.depthOfField.focusTargetZ = extractFloat(args[2], "PostProcess.dof.setFocusTarget z");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocusSmoothing",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.focusSmoothing);
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_setFocusSmoothing",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.depthOfField.focusSmoothing = extractFloat(args[0], "PostProcess.dof.setFocusSmoothing");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_getFocalRange",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.focalRange);
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_setFocalRange",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.depthOfField.focalRange = extractFloat(args[0], "PostProcess.dof.setFocalRange");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_getMaxBlurRadius",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.depthOfField.maxBlurRadius);
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_setMaxBlurRadius",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.depthOfField.maxBlurRadius = extractFloat(args[0], "PostProcess.dof.setMaxBlurRadius");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_getSampleCount",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.depthOfField.sampleCount));
            }});

        interpreter->registerNativeFunction("_native_postprocess_dof_setSampleCount",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t count = extractInt64(args[0], "PostProcess.dof.setSampleCount");
                    if (count >= 4 && count <= 32)
                    {
                        s.depthOfField.sampleCount = static_cast<int>(count);
                    }
                });
            }});

        // =============================================
        // Volumetric Fog
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.enabled);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.enabled = extractBool(args[0]);
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getQuality",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.volumetricFog.quality));
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setQuality",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t quality = extractInt64(args[0], "PostProcess.volumetricFog.setQuality");
                    if (quality >= 0 && quality <= 2)
                    {
                        s.volumetricFog.quality = static_cast<postprocess::VolumetricQuality>(quality);
                    }
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getDensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.uniformDensity);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setDensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.uniformDensity = extractFloat(args[0], "PostProcess.volumetricFog.setDensity");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getColorR",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.fogColor[0]);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getColorG",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.fogColor[1]);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getColorB",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.fogColor[2]);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setColor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.fogColor[0] = extractFloat(args[0], "PostProcess.volumetricFog.setColor r");
                    s.volumetricFog.fogColor[1] = extractFloat(args[1], "PostProcess.volumetricFog.setColor g");
                    s.volumetricFog.fogColor[2] = extractFloat(args[2], "PostProcess.volumetricFog.setColor b");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getHeightFogDensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.heightFogDensity);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setHeightFogDensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.heightFogDensity = extractFloat(args[0], "PostProcess.volumetricFog.setHeightFogDensity");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getHeightFogFalloff",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.heightFogFalloff);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setHeightFogFalloff",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.heightFogFalloff = extractFloat(args[0], "PostProcess.volumetricFog.setHeightFogFalloff");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getHeightFogOffset",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.heightFogOffset);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setHeightFogOffset",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.heightFogOffset = extractFloat(args[0], "PostProcess.volumetricFog.setHeightFogOffset");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getScatteringCoefficient",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.scatteringCoefficient);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setScatteringCoefficient",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.scatteringCoefficient = extractFloat(args[0], "PostProcess.volumetricFog.setScatteringCoefficient");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getAbsorptionCoefficient",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.absorptionCoefficient);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setAbsorptionCoefficient",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.absorptionCoefficient = extractFloat(args[0], "PostProcess.volumetricFog.setAbsorptionCoefficient");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getAnisotropy",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.anisotropy);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setAnisotropy",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.anisotropy = extractFloat(args[0], "PostProcess.volumetricFog.setAnisotropy");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.intensity);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.intensity = extractFloat(args[0], "PostProcess.volumetricFog.setIntensity");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getAmbientIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.ambientIntensity);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setAmbientIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.ambientIntensity = extractFloat(args[0], "PostProcess.volumetricFog.setAmbientIntensity");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getMaxDistance",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.maxDistance);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setMaxDistance",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.maxDistance = extractFloat(args[0], "PostProcess.volumetricFog.setMaxDistance");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_getTemporalBlendFactor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.volumetricFog.temporalBlendFactor);
            }});

        interpreter->registerNativeFunction("_native_postprocess_volumetricFog_setTemporalBlendFactor",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.volumetricFog.temporalBlendFactor = extractFloat(args[0], "PostProcess.volumetricFog.setTemporalBlendFactor");
                });
            }});

        // =============================================
        // SSAO
        // =============================================

        interpreter->registerNativeFunction("_native_postprocess_ssao_isEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.ssao.enabled);
            }});

        interpreter->registerNativeFunction("_native_postprocess_ssao_setEnabled",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.ssao.enabled = extractBool(args[0]);
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_ssao_getRadius",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.ssao.radius);
            }});

        interpreter->registerNativeFunction("_native_postprocess_ssao_setRadius",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.ssao.radius = extractFloat(args[0], "PostProcess.ssao.setRadius");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_ssao_getBias",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.ssao.bias);
            }});

        interpreter->registerNativeFunction("_native_postprocess_ssao_setBias",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.ssao.bias = extractFloat(args[0], "PostProcess.ssao.setBias");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_ssao_getIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.ssao.intensity);
            }});

        interpreter->registerNativeFunction("_native_postprocess_ssao_setIntensity",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.ssao.intensity = extractFloat(args[0], "PostProcess.ssao.setIntensity");
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_ssao_getKernelSize",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.ssao.kernelSize));
            }});

        interpreter->registerNativeFunction("_native_postprocess_ssao_setKernelSize",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t size = extractInt64(args[0], "PostProcess.ssao.setKernelSize");
                    if (size >= 8 && size <= 64)
                    {
                        s.ssao.kernelSize = static_cast<int>(size);
                    }
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_ssao_getQuality",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(static_cast<int64_t>(s.ssao.quality));
            }});

        interpreter->registerNativeFunction("_native_postprocess_ssao_setQuality",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    int64_t q = extractInt64(args[0], "PostProcess.ssao.setQuality");
                    if (q >= 0 && q <= 3)
                    {
                        s.ssao.quality = static_cast<postprocess::SSAOQuality>(q);
                        s.ssao.kernelSize = postprocess::ssaoSamplesFromQuality(s.ssao.quality);
                    }
                });
            }});

        interpreter->registerNativeFunction("_native_postprocess_ssao_getPower",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value>) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                auto s = dispatcher.query(events::postprocess::GetPostProcessSettingsQuery{});
                return value::Value(s.ssao.power);
            }});

        interpreter->registerNativeFunction("_native_postprocess_ssao_setPower",
            {nullptr, [](void*, environment::NativeContext&, std::span<const value::Value> args) -> value::Value{
                auto& dispatcher = events::EventDispatcher::instance();
                return modifySettings(dispatcher, [&](postprocess::PostProcessSettings& s)
                {
                    s.ssao.power = extractFloat(args[0], "PostProcess.ssao.setPower");
                });
            }});

    }
}
