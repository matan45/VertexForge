#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"
#include <algorithm>

namespace serialization
{
    namespace
    {
        std::string toneMappingModeToStr(postprocess::ToneMappingMode mode)
        {
            switch (mode)
            {
            case postprocess::ToneMappingMode::ACES: return "aces";
            case postprocess::ToneMappingMode::Reinhard: return "reinhard";
            case postprocess::ToneMappingMode::Uncharted2: return "uncharted2";
            case postprocess::ToneMappingMode::Linear: return "linear";
            default: return "aces";
            }
        }

        postprocess::ToneMappingMode strToToneMappingMode(const std::string& str)
        {
            if (str == "aces") return postprocess::ToneMappingMode::ACES;
            if (str == "reinhard") return postprocess::ToneMappingMode::Reinhard;
            if (str == "uncharted2") return postprocess::ToneMappingMode::Uncharted2;
            if (str == "linear") return postprocess::ToneMappingMode::Linear;
            return postprocess::ToneMappingMode::ACES;
        }
        json serializeToneMapping(const postprocess::ToneMappingSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"mode", toneMappingModeToStr(s.mode)},
                {"exposure", s.exposure},
                {"gamma", s.gamma},
                {"contrast", s.contrast},
                {"toe", s.toe},
                {"shoulder", s.shoulder}
            };
        }

        json serializeUpscale(const postprocess::UpscaleSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"mode", static_cast<int>(s.mode)},
                {"quality", static_cast<int>(s.quality)}
            };
        }

        json serializeFrameGen(const postprocess::FrameGenSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"numFramesToGenerate", s.numFramesToGenerate}
            };
        }

        json serializeReflex(const postprocess::ReflexSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"mode", static_cast<int>(s.mode)}
            };
        }

        json serializeBloom(const postprocess::BloomSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"threshold", s.threshold},
                {"intensity", s.intensity},
                {"radius", s.radius},
                {"passes", s.passes}
            };
        }

        json serializeVignette(const postprocess::VignetteSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"intensity", s.intensity},
                {"radius", s.radius},
                {"softness", s.softness}
            };
        }

        json serializeChromaticAberration(const postprocess::ChromaticAberrationSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"intensity", s.intensity}
            };
        }

        json serializeFilmGrain(const postprocess::FilmGrainSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"intensity", s.intensity},
                {"size", s.size}
            };
        }

        json serializeDepthOfField(const postprocess::DepthOfFieldSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"focusMode", static_cast<int>(s.focusMode)},
                {"focalDistance", s.focalDistance},
                {"focusTargetX", s.focusTargetX},
                {"focusTargetY", s.focusTargetY},
                {"focusTargetZ", s.focusTargetZ},
                {"focusSmoothing", s.focusSmoothing},
                {"focalRange", s.focalRange},
                {"maxBlurRadius", s.maxBlurRadius},
                {"sampleCount", s.sampleCount}
            };
        }

        std::string volumetricQualityToStr(postprocess::VolumetricQuality quality)
        {
            switch (quality)
            {
            case postprocess::VolumetricQuality::Low: return "low";
            case postprocess::VolumetricQuality::Medium: return "medium";
            case postprocess::VolumetricQuality::High: return "high";
            default: return "medium";
            }
        }

        postprocess::VolumetricQuality strToVolumetricQuality(const std::string& str)
        {
            if (str == "low") return postprocess::VolumetricQuality::Low;
            if (str == "medium") return postprocess::VolumetricQuality::Medium;
            if (str == "high") return postprocess::VolumetricQuality::High;
            return postprocess::VolumetricQuality::Medium;
        }
        json serializeSSAO(const postprocess::SSAOSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"quality", static_cast<int>(s.quality)},
                {"radius", s.radius},
                {"bias", s.bias},
                {"intensity", s.intensity},
                {"kernelSize", s.kernelSize},
                {"power", s.power}
            };
        }

        json serializeEdgeDetection(const postprocess::EdgeDetectionSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"threshold", s.threshold},
                {"edgeWidth", s.edgeWidth},
                {"edgeColor", {s.edgeColor[0], s.edgeColor[1], s.edgeColor[2]}},
                {"opacity", s.opacity}
            };
        }

        json serializeAutoExposure(const postprocess::AutoExposureSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"minExposure", s.minExposure},
                {"maxExposure", s.maxExposure},
                {"adaptSpeedUp", s.adaptSpeedUp},
                {"adaptSpeedDown", s.adaptSpeedDown},
                {"exposureCompensation", s.exposureCompensation},
                {"lowPercentile", s.lowPercentile},
                {"highPercentile", s.highPercentile}
            };
        }

        json serializeVolumetricFog(const postprocess::VolumetricFogSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"quality", volumetricQualityToStr(s.quality)},
                {"uniformDensity", s.uniformDensity},
                {"fogColor", {s.fogColor[0], s.fogColor[1], s.fogColor[2]}},
                {"heightFogDensity", s.heightFogDensity},
                {"heightFogFalloff", s.heightFogFalloff},
                {"heightFogOffset", s.heightFogOffset},
                {"scatteringCoefficient", s.scatteringCoefficient},
                {"absorptionCoefficient", s.absorptionCoefficient},
                {"anisotropy", s.anisotropy},
                {"temporalBlendFactor", s.temporalBlendFactor},
                {"intensity", s.intensity},
                {"ambientIntensity", s.ambientIntensity},
                {"maxDistance", s.maxDistance},
                {"giInjectionIntensity", s.giInjectionIntensity},
                {"noiseEnabled", s.noiseEnabled},
                {"noiseScale", s.noiseScale},
                {"noiseIntensity", s.noiseIntensity},
                {"noiseSpeed", s.noiseSpeed},
                {"noiseOctaves", s.noiseOctaves}
            };
        }

        json serializeUnderwater(const postprocess::UnderwaterSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"fogDensity", s.fogDensity},
                {"fogColor", {s.fogColor[0], s.fogColor[1], s.fogColor[2]}},
                {"absorptionR", s.absorptionR},
                {"absorptionG", s.absorptionG},
                {"absorptionB", s.absorptionB},
                {"causticStrength", s.causticStrength},
                {"causticScale", s.causticScale},
                {"causticSpeed", s.causticSpeed},
                {"meniscusWidth", s.meniscusWidth},
                {"meniscusDistortion", s.meniscusDistortion},
                {"chromaticStrength", s.chromaticStrength},
                {"maxFogDistance", s.maxFogDistance}
            };
        }

        json serializeColorGrading(const postprocess::ColorGradingSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"primaryLutPath", s.primaryLutPath},
                {"secondaryLutPath", s.secondaryLutPath},
                {"lutIntensity", s.lutIntensity},
                {"lutBlendFactor", s.lutBlendFactor},
                {"liftR", s.liftR}, {"liftG", s.liftG}, {"liftB", s.liftB},
                {"gammaR", s.gammaR}, {"gammaG", s.gammaG}, {"gammaB", s.gammaB},
                {"gainR", s.gainR}, {"gainG", s.gainG}, {"gainB", s.gainB},
                {"saturation", s.saturation},
                {"colorTemperature", s.colorTemperature},
                {"colorTint", s.colorTint}
            };
        }

        void deserializeToneMapping(const json& j, postprocess::ToneMappingSettings& s)
        {
            if (!j.contains("toneMapping") || !j["toneMapping"].is_object())
                return;
            const auto& tm = j["toneMapping"];
            if (tm.contains("enabled") && tm["enabled"].is_boolean())
                s.enabled = tm["enabled"].get<bool>();
            if (tm.contains("mode") && tm["mode"].is_string())
                s.mode = strToToneMappingMode(tm["mode"].get<std::string>());
            if (tm.contains("exposure") && tm["exposure"].is_number())
                s.exposure = std::clamp(tm["exposure"].get<float>(), 0.01f, 20.0f);
            if (tm.contains("gamma") && tm["gamma"].is_number())
                s.gamma = std::clamp(tm["gamma"].get<float>(), 0.1f, 5.0f);
            if (tm.contains("contrast") && tm["contrast"].is_number())
                s.contrast = std::clamp(tm["contrast"].get<float>(), 0.5f, 2.0f);
            if (tm.contains("toe") && tm["toe"].is_number())
                s.toe = std::clamp(tm["toe"].get<float>(), 0.0f, 1.0f);
            if (tm.contains("shoulder") && tm["shoulder"].is_number())
                s.shoulder = std::clamp(tm["shoulder"].get<float>(), 0.0f, 1.0f);
        }

        void deserializeUpscale(const json& j, postprocess::UpscaleSettings& s)
        {
            if (!j.contains("upscale") || !j["upscale"].is_object())
                return;
            const auto& u = j["upscale"];
            if (u.contains("enabled") && u["enabled"].is_boolean())
                s.enabled = u["enabled"].get<bool>();
            if (u.contains("mode") && u["mode"].is_number_integer())
                s.mode = static_cast<postprocess::UpscaleMode>(
                    std::clamp(u["mode"].get<int>(), 0, 2));
            if (u.contains("quality") && u["quality"].is_number_integer())
                s.quality = static_cast<postprocess::UpscaleQuality>(
                    std::clamp(u["quality"].get<int>(), 0, 4));
        }

        void deserializeFrameGen(const json& j, postprocess::FrameGenSettings& s)
        {
            if (!j.contains("frameGen") || !j["frameGen"].is_object())
                return;
            const auto& fg = j["frameGen"];
            if (fg.contains("enabled") && fg["enabled"].is_boolean())
                s.enabled = fg["enabled"].get<bool>();
            if (fg.contains("numFramesToGenerate") && fg["numFramesToGenerate"].is_number_unsigned())
                s.numFramesToGenerate = std::clamp(fg["numFramesToGenerate"].get<uint32_t>(), 1u, 3u);
        }

        void deserializeReflex(const json& j, postprocess::ReflexSettings& s)
        {
            if (!j.contains("reflex") || !j["reflex"].is_object())
                return;
            const auto& r = j["reflex"];
            if (r.contains("enabled") && r["enabled"].is_boolean())
                s.enabled = r["enabled"].get<bool>();
            if (r.contains("mode") && r["mode"].is_number_integer())
                s.mode = static_cast<postprocess::ReflexMode>(
                    std::clamp(r["mode"].get<int>(), 0, 2));
        }

        void deserializeBloom(const json& j, postprocess::BloomSettings& s)
        {
            if (!j.contains("bloom") || !j["bloom"].is_object())
                return;
            const auto& bloom = j["bloom"];
            if (bloom.contains("enabled") && bloom["enabled"].is_boolean())
                s.enabled = bloom["enabled"].get<bool>();
            if (bloom.contains("threshold") && bloom["threshold"].is_number())
                s.threshold = std::clamp(bloom["threshold"].get<float>(), 0.0f, 10.0f);
            if (bloom.contains("intensity") && bloom["intensity"].is_number())
                s.intensity = std::clamp(bloom["intensity"].get<float>(), 0.0f, 5.0f);
            if (bloom.contains("radius") && bloom["radius"].is_number())
                s.radius = std::clamp(bloom["radius"].get<float>(), 0.0f, 1.0f);
            if (bloom.contains("passes") && bloom["passes"].is_number_unsigned())
                s.passes = std::clamp(bloom["passes"].get<uint32_t>(), 1u, 10u);
        }

        void deserializeVignette(const json& j, postprocess::VignetteSettings& s)
        {
            if (!j.contains("vignette") || !j["vignette"].is_object())
                return;
            const auto& vignette = j["vignette"];
            if (vignette.contains("enabled") && vignette["enabled"].is_boolean())
                s.enabled = vignette["enabled"].get<bool>();
            if (vignette.contains("intensity") && vignette["intensity"].is_number())
                s.intensity = std::clamp(vignette["intensity"].get<float>(), 0.0f, 1.0f);
            if (vignette.contains("radius") && vignette["radius"].is_number())
                s.radius = std::clamp(vignette["radius"].get<float>(), 0.0f, 2.0f);
            if (vignette.contains("softness") && vignette["softness"].is_number())
                s.softness = std::clamp(vignette["softness"].get<float>(), 0.0f, 1.0f);
        }

        void deserializeChromaticAberration(const json& j, postprocess::ChromaticAberrationSettings& s)
        {
            if (!j.contains("chromaticAberration") || !j["chromaticAberration"].is_object())
                return;
            const auto& ca = j["chromaticAberration"];
            if (ca.contains("enabled") && ca["enabled"].is_boolean())
                s.enabled = ca["enabled"].get<bool>();
            if (ca.contains("intensity") && ca["intensity"].is_number())
                s.intensity = std::clamp(ca["intensity"].get<float>(), 0.0f, 0.1f);
        }

        void deserializeFilmGrain(const json& j, postprocess::FilmGrainSettings& s)
        {
            if (!j.contains("filmGrain") || !j["filmGrain"].is_object())
                return;
            const auto& fg = j["filmGrain"];
            if (fg.contains("enabled") && fg["enabled"].is_boolean())
                s.enabled = fg["enabled"].get<bool>();
            if (fg.contains("intensity") && fg["intensity"].is_number())
                s.intensity = std::clamp(fg["intensity"].get<float>(), 0.0f, 1.0f);
            if (fg.contains("size") && fg["size"].is_number())
                s.size = std::clamp(fg["size"].get<float>(), 0.1f, 5.0f);
        }

        void deserializeDepthOfField(const json& j, postprocess::DepthOfFieldSettings& s)
        {
            if (!j.contains("depthOfField") || !j["depthOfField"].is_object())
                return;
            const auto& df = j["depthOfField"];
            if (df.contains("enabled") && df["enabled"].is_boolean())
                s.enabled = df["enabled"].get<bool>();
            if (df.contains("focusMode") && df["focusMode"].is_number_integer())
                s.focusMode = static_cast<postprocess::DoFFocusMode>(
                    std::clamp(df["focusMode"].get<int>(), 0, 1));
            if (df.contains("focalDistance") && df["focalDistance"].is_number())
                s.focalDistance = std::clamp(df["focalDistance"].get<float>(), 0.1f, 1000.0f);
            if (df.contains("focusTargetX") && df["focusTargetX"].is_number())
                s.focusTargetX = df["focusTargetX"].get<float>();
            if (df.contains("focusTargetY") && df["focusTargetY"].is_number())
                s.focusTargetY = df["focusTargetY"].get<float>();
            if (df.contains("focusTargetZ") && df["focusTargetZ"].is_number())
                s.focusTargetZ = df["focusTargetZ"].get<float>();
            if (df.contains("focusSmoothing") && df["focusSmoothing"].is_number())
                s.focusSmoothing = std::clamp(df["focusSmoothing"].get<float>(), 0.1f, 50.0f);
            if (df.contains("focalRange") && df["focalRange"].is_number())
                s.focalRange = std::clamp(df["focalRange"].get<float>(), 0.1f, 100.0f);
            if (df.contains("maxBlurRadius") && df["maxBlurRadius"].is_number())
                s.maxBlurRadius = std::clamp(df["maxBlurRadius"].get<float>(), 0.0f, 20.0f);
            if (df.contains("sampleCount") && df["sampleCount"].is_number_integer())
                s.sampleCount = std::clamp(df["sampleCount"].get<int>(), 4, 32);
        }

        void deserializeVolumetricFog(const json& j, postprocess::VolumetricFogSettings& s)
        {
            if (!j.contains("volumetricFog") || !j["volumetricFog"].is_object())
                return;
            const auto& vf = j["volumetricFog"];
            if (vf.contains("enabled") && vf["enabled"].is_boolean())
                s.enabled = vf["enabled"].get<bool>();
            if (vf.contains("quality") && vf["quality"].is_string())
                s.quality = strToVolumetricQuality(vf["quality"].get<std::string>());
            if (vf.contains("uniformDensity") && vf["uniformDensity"].is_number())
                s.uniformDensity = std::clamp(vf["uniformDensity"].get<float>(), 0.0f, 1.0f);
            if (vf.contains("fogColor") && vf["fogColor"].is_array() && vf["fogColor"].size() == 3)
            {
                s.fogColor[0] = std::clamp(vf["fogColor"][0].get<float>(), 0.0f, 1.0f);
                s.fogColor[1] = std::clamp(vf["fogColor"][1].get<float>(), 0.0f, 1.0f);
                s.fogColor[2] = std::clamp(vf["fogColor"][2].get<float>(), 0.0f, 1.0f);
            }
            if (vf.contains("heightFogDensity") && vf["heightFogDensity"].is_number())
                s.heightFogDensity = std::clamp(vf["heightFogDensity"].get<float>(), 0.0f, 1.0f);
            if (vf.contains("heightFogFalloff") && vf["heightFogFalloff"].is_number())
                s.heightFogFalloff = std::clamp(vf["heightFogFalloff"].get<float>(), 0.0f, 5.0f);
            if (vf.contains("heightFogOffset") && vf["heightFogOffset"].is_number())
                s.heightFogOffset = std::clamp(vf["heightFogOffset"].get<float>(), -100.0f, 100.0f);
            if (vf.contains("scatteringCoefficient") && vf["scatteringCoefficient"].is_number())
                s.scatteringCoefficient = std::clamp(vf["scatteringCoefficient"].get<float>(), 0.0f, 5.0f);
            if (vf.contains("absorptionCoefficient") && vf["absorptionCoefficient"].is_number())
                s.absorptionCoefficient = std::clamp(vf["absorptionCoefficient"].get<float>(), 0.0f, 5.0f);
            if (vf.contains("anisotropy") && vf["anisotropy"].is_number())
                s.anisotropy = std::clamp(vf["anisotropy"].get<float>(), -1.0f, 1.0f);
            if (vf.contains("temporalBlendFactor") && vf["temporalBlendFactor"].is_number())
                s.temporalBlendFactor = std::clamp(vf["temporalBlendFactor"].get<float>(), 0.0f, 1.0f);
            if (vf.contains("intensity") && vf["intensity"].is_number())
                s.intensity = std::clamp(vf["intensity"].get<float>(), 0.0f, 5.0f);
            if (vf.contains("ambientIntensity") && vf["ambientIntensity"].is_number())
                s.ambientIntensity = std::clamp(vf["ambientIntensity"].get<float>(), 0.0f, 2.0f);
            if (vf.contains("maxDistance") && vf["maxDistance"].is_number())
                s.maxDistance = std::clamp(vf["maxDistance"].get<float>(), 10.0f, 5000.0f);
            if (vf.contains("giInjectionIntensity") && vf["giInjectionIntensity"].is_number())
                s.giInjectionIntensity = std::clamp(vf["giInjectionIntensity"].get<float>(), 0.0f, 5.0f);
            if (vf.contains("noiseEnabled") && vf["noiseEnabled"].is_boolean())
                s.noiseEnabled = vf["noiseEnabled"].get<bool>();
            if (vf.contains("noiseScale") && vf["noiseScale"].is_number())
                s.noiseScale = std::clamp(vf["noiseScale"].get<float>(), 0.001f, 1.0f);
            if (vf.contains("noiseIntensity") && vf["noiseIntensity"].is_number())
                s.noiseIntensity = std::clamp(vf["noiseIntensity"].get<float>(), 0.0f, 1.0f);
            if (vf.contains("noiseSpeed") && vf["noiseSpeed"].is_number())
                s.noiseSpeed = std::clamp(vf["noiseSpeed"].get<float>(), 0.0f, 2.0f);
            if (vf.contains("noiseOctaves") && vf["noiseOctaves"].is_number_integer())
                s.noiseOctaves = std::clamp(vf["noiseOctaves"].get<int>(), 1, 4);
        }

        void deserializeSSAO(const json& j, postprocess::SSAOSettings& s)
        {
            if (!j.contains("ssao") || !j["ssao"].is_object())
                return;
            const auto& ao = j["ssao"];
            if (ao.contains("enabled") && ao["enabled"].is_boolean())
                s.enabled = ao["enabled"].get<bool>();
            if (ao.contains("quality") && ao["quality"].is_number_integer())
            {
                int q = std::clamp(ao["quality"].get<int>(), 0, 3);
                s.quality = static_cast<postprocess::SSAOQuality>(q);
            }
            if (ao.contains("radius") && ao["radius"].is_number())
                s.radius = std::clamp(ao["radius"].get<float>(), 0.1f, 5.0f);
            if (ao.contains("bias") && ao["bias"].is_number())
                s.bias = std::clamp(ao["bias"].get<float>(), 0.001f, 0.1f);
            if (ao.contains("intensity") && ao["intensity"].is_number())
                s.intensity = std::clamp(ao["intensity"].get<float>(), 0.1f, 5.0f);
            if (ao.contains("kernelSize") && ao["kernelSize"].is_number_integer())
                s.kernelSize = std::clamp(ao["kernelSize"].get<int>(), 8, 64);
            if (ao.contains("power") && ao["power"].is_number())
                s.power = std::clamp(ao["power"].get<float>(), 0.5f, 5.0f);
        }

        void deserializeEdgeDetection(const json& j, postprocess::EdgeDetectionSettings& s)
        {
            if (!j.contains("edgeDetection") || !j["edgeDetection"].is_object())
                return;
            const auto& ed = j["edgeDetection"];
            if (ed.contains("enabled") && ed["enabled"].is_boolean())
                s.enabled = ed["enabled"].get<bool>();
            if (ed.contains("threshold") && ed["threshold"].is_number())
                s.threshold = std::clamp(ed["threshold"].get<float>(), 0.01f, 1.0f);
            if (ed.contains("edgeWidth") && ed["edgeWidth"].is_number())
                s.edgeWidth = std::clamp(ed["edgeWidth"].get<float>(), 0.5f, 3.0f);
            if (ed.contains("edgeColor") && ed["edgeColor"].is_array() && ed["edgeColor"].size() == 3)
            {
                s.edgeColor[0] = std::clamp(ed["edgeColor"][0].get<float>(), 0.0f, 1.0f);
                s.edgeColor[1] = std::clamp(ed["edgeColor"][1].get<float>(), 0.0f, 1.0f);
                s.edgeColor[2] = std::clamp(ed["edgeColor"][2].get<float>(), 0.0f, 1.0f);
            }
            if (ed.contains("opacity") && ed["opacity"].is_number())
                s.opacity = std::clamp(ed["opacity"].get<float>(), 0.0f, 1.0f);
        }

        void deserializeAutoExposure(const json& j, postprocess::AutoExposureSettings& s)
        {
            if (!j.contains("autoExposure") || !j["autoExposure"].is_object())
                return;
            const auto& ae = j["autoExposure"];
            if (ae.contains("enabled") && ae["enabled"].is_boolean())
                s.enabled = ae["enabled"].get<bool>();
            if (ae.contains("minExposure") && ae["minExposure"].is_number())
                s.minExposure = std::clamp(ae["minExposure"].get<float>(), 0.01f, 1.0f);
            if (ae.contains("maxExposure") && ae["maxExposure"].is_number())
                s.maxExposure = std::clamp(ae["maxExposure"].get<float>(), 1.0f, 20.0f);
            if (ae.contains("adaptSpeedUp") && ae["adaptSpeedUp"].is_number())
                s.adaptSpeedUp = std::clamp(ae["adaptSpeedUp"].get<float>(), 0.1f, 10.0f);
            if (ae.contains("adaptSpeedDown") && ae["adaptSpeedDown"].is_number())
                s.adaptSpeedDown = std::clamp(ae["adaptSpeedDown"].get<float>(), 0.1f, 10.0f);
            if (ae.contains("exposureCompensation") && ae["exposureCompensation"].is_number())
                s.exposureCompensation = std::clamp(ae["exposureCompensation"].get<float>(), -5.0f, 5.0f);
            if (ae.contains("lowPercentile") && ae["lowPercentile"].is_number())
                s.lowPercentile = std::clamp(ae["lowPercentile"].get<float>(), 0.0f, 0.5f);
            if (ae.contains("highPercentile") && ae["highPercentile"].is_number())
                s.highPercentile = std::clamp(ae["highPercentile"].get<float>(), 0.5f, 1.0f);
        }

        void deserializeUnderwater(const json& j, postprocess::UnderwaterSettings& s)
        {
            if (!j.contains("underwater") || !j["underwater"].is_object())
                return;
            const auto& uw = j["underwater"];
            if (uw.contains("enabled") && uw["enabled"].is_boolean())
                s.enabled = uw["enabled"].get<bool>();
            if (uw.contains("fogDensity") && uw["fogDensity"].is_number())
                s.fogDensity = std::clamp(uw["fogDensity"].get<float>(), 0.0f, 1.0f);
            if (uw.contains("fogColor") && uw["fogColor"].is_array() && uw["fogColor"].size() == 3)
            {
                s.fogColor[0] = std::clamp(uw["fogColor"][0].get<float>(), 0.0f, 1.0f);
                s.fogColor[1] = std::clamp(uw["fogColor"][1].get<float>(), 0.0f, 1.0f);
                s.fogColor[2] = std::clamp(uw["fogColor"][2].get<float>(), 0.0f, 1.0f);
            }
            if (uw.contains("absorptionR") && uw["absorptionR"].is_number())
                s.absorptionR = std::clamp(uw["absorptionR"].get<float>(), 0.0f, 1.0f);
            if (uw.contains("absorptionG") && uw["absorptionG"].is_number())
                s.absorptionG = std::clamp(uw["absorptionG"].get<float>(), 0.0f, 1.0f);
            if (uw.contains("absorptionB") && uw["absorptionB"].is_number())
                s.absorptionB = std::clamp(uw["absorptionB"].get<float>(), 0.0f, 1.0f);
            if (uw.contains("causticStrength") && uw["causticStrength"].is_number())
                s.causticStrength = std::clamp(uw["causticStrength"].get<float>(), 0.0f, 2.0f);
            if (uw.contains("causticScale") && uw["causticScale"].is_number())
                s.causticScale = std::clamp(uw["causticScale"].get<float>(), 10.0f, 200.0f);
            if (uw.contains("causticSpeed") && uw["causticSpeed"].is_number())
                s.causticSpeed = std::clamp(uw["causticSpeed"].get<float>(), 0.0f, 2.0f);
            if (uw.contains("meniscusWidth") && uw["meniscusWidth"].is_number())
                s.meniscusWidth = std::clamp(uw["meniscusWidth"].get<float>(), 0.0f, 0.1f);
            if (uw.contains("meniscusDistortion") && uw["meniscusDistortion"].is_number())
                s.meniscusDistortion = std::clamp(uw["meniscusDistortion"].get<float>(), 0.0f, 0.1f);
            if (uw.contains("chromaticStrength") && uw["chromaticStrength"].is_number())
                s.chromaticStrength = std::clamp(uw["chromaticStrength"].get<float>(), 0.0f, 0.01f);
            if (uw.contains("maxFogDistance") && uw["maxFogDistance"].is_number())
                s.maxFogDistance = std::clamp(uw["maxFogDistance"].get<float>(), 10.0f, 500.0f);
        }

        void deserializeColorGrading(const json& j, postprocess::ColorGradingSettings& s)
        {
            if (!j.contains("colorGrading") || !j["colorGrading"].is_object())
                return;
            const auto& cg = j["colorGrading"];
            if (cg.contains("enabled") && cg["enabled"].is_boolean())
                s.enabled = cg["enabled"].get<bool>();
            if (cg.contains("primaryLutPath") && cg["primaryLutPath"].is_string())
                s.primaryLutPath = cg["primaryLutPath"].get<std::string>();
            if (cg.contains("secondaryLutPath") && cg["secondaryLutPath"].is_string())
                s.secondaryLutPath = cg["secondaryLutPath"].get<std::string>();
            if (cg.contains("lutIntensity") && cg["lutIntensity"].is_number())
                s.lutIntensity = std::clamp(cg["lutIntensity"].get<float>(), 0.0f, 1.0f);
            if (cg.contains("lutBlendFactor") && cg["lutBlendFactor"].is_number())
                s.lutBlendFactor = std::clamp(cg["lutBlendFactor"].get<float>(), 0.0f, 1.0f);
            if (cg.contains("liftR") && cg["liftR"].is_number())
                s.liftR = std::clamp(cg["liftR"].get<float>(), -1.0f, 1.0f);
            if (cg.contains("liftG") && cg["liftG"].is_number())
                s.liftG = std::clamp(cg["liftG"].get<float>(), -1.0f, 1.0f);
            if (cg.contains("liftB") && cg["liftB"].is_number())
                s.liftB = std::clamp(cg["liftB"].get<float>(), -1.0f, 1.0f);
            if (cg.contains("gammaR") && cg["gammaR"].is_number())
                s.gammaR = std::clamp(cg["gammaR"].get<float>(), 0.01f, 5.0f);
            if (cg.contains("gammaG") && cg["gammaG"].is_number())
                s.gammaG = std::clamp(cg["gammaG"].get<float>(), 0.01f, 5.0f);
            if (cg.contains("gammaB") && cg["gammaB"].is_number())
                s.gammaB = std::clamp(cg["gammaB"].get<float>(), 0.01f, 5.0f);
            if (cg.contains("gainR") && cg["gainR"].is_number())
                s.gainR = std::clamp(cg["gainR"].get<float>(), 0.0f, 5.0f);
            if (cg.contains("gainG") && cg["gainG"].is_number())
                s.gainG = std::clamp(cg["gainG"].get<float>(), 0.0f, 5.0f);
            if (cg.contains("gainB") && cg["gainB"].is_number())
                s.gainB = std::clamp(cg["gainB"].get<float>(), 0.0f, 5.0f);
            if (cg.contains("saturation") && cg["saturation"].is_number())
                s.saturation = std::clamp(cg["saturation"].get<float>(), 0.0f, 3.0f);
            if (cg.contains("colorTemperature") && cg["colorTemperature"].is_number())
                s.colorTemperature = std::clamp(cg["colorTemperature"].get<float>(), 1000.0f, 15000.0f);
            if (cg.contains("colorTint") && cg["colorTint"].is_number())
                s.colorTint = std::clamp(cg["colorTint"].get<float>(), -1.0f, 1.0f);
        }

    } // anonymous namespace

    json SceneSerialization::serializePostProcessSettings(const postprocess::PostProcessSettings& settings)
    {
        json j;
        j["enabled"] = settings.enabled;
        j["toneMapping"] = serializeToneMapping(settings.toneMapping);
        j["upscale"] = serializeUpscale(settings.upscale);
        j["frameGen"] = serializeFrameGen(settings.frameGen);
        j["reflex"] = serializeReflex(settings.reflex);
        j["bloom"] = serializeBloom(settings.bloom);
        j["vignette"] = serializeVignette(settings.vignette);
        j["chromaticAberration"] = serializeChromaticAberration(settings.chromaticAberration);
        j["filmGrain"] = serializeFilmGrain(settings.filmGrain);
        j["depthOfField"] = serializeDepthOfField(settings.depthOfField);
        j["volumetricFog"] = serializeVolumetricFog(settings.volumetricFog);
        j["ssao"] = serializeSSAO(settings.ssao);
        j["edgeDetection"] = serializeEdgeDetection(settings.edgeDetection);
        j["autoExposure"] = serializeAutoExposure(settings.autoExposure);
        j["colorGrading"] = serializeColorGrading(settings.colorGrading);
        j["underwater"] = serializeUnderwater(settings.underwater);
        return j;
    }

    void SceneSerialization::deserializePostProcessSettings(const json& j, postprocess::PostProcessSettings& settings)
    {
        if (j.contains("enabled") && j["enabled"].is_boolean())
            settings.enabled = j["enabled"].get<bool>();
        deserializeToneMapping(j, settings.toneMapping);
        deserializeUpscale(j, settings.upscale);
        deserializeFrameGen(j, settings.frameGen);
        deserializeReflex(j, settings.reflex);
        deserializeBloom(j, settings.bloom);
        deserializeVignette(j, settings.vignette);
        deserializeChromaticAberration(j, settings.chromaticAberration);
        deserializeFilmGrain(j, settings.filmGrain);
        deserializeDepthOfField(j, settings.depthOfField);
        deserializeVolumetricFog(j, settings.volumetricFog);
        deserializeSSAO(j, settings.ssao);
        deserializeEdgeDetection(j, settings.edgeDetection);
        deserializeAutoExposure(j, settings.autoExposure);
        deserializeColorGrading(j, settings.colorGrading);
        deserializeUnderwater(j, settings.underwater);
    }
}
