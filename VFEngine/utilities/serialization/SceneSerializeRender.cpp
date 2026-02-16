#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"
#include "../print/EditorLogger.hpp"
#include <algorithm>

namespace serialization
{
    namespace
    {
        // ---- Enum/string conversions for shadows/cascade ----

        std::string shadowQualityToStr(types::ShadowQuality quality)
        {
            switch (quality)
            {
            case types::ShadowQuality::Off: return "off";
            case types::ShadowQuality::Low: return "low";
            case types::ShadowQuality::Medium: return "medium";
            case types::ShadowQuality::High: return "high";
            case types::ShadowQuality::Ultra: return "ultra";
            default: return "high";
            }
        }

        types::ShadowQuality strToShadowQuality(const std::string& str)
        {
            if (str == "off") return types::ShadowQuality::Off;
            if (str == "low") return types::ShadowQuality::Low;
            if (str == "medium") return types::ShadowQuality::Medium;
            if (str == "high") return types::ShadowQuality::High;
            if (str == "ultra") return types::ShadowQuality::Ultra;
            return types::ShadowQuality::High;
        }

        std::string cascadeSplitModeToStr(types::CascadeSplitMode mode)
        {
            switch (mode)
            {
            case types::CascadeSplitMode::Linear: return "linear";
            case types::CascadeSplitMode::Logarithmic: return "logarithmic";
            case types::CascadeSplitMode::Practical: return "practical";
            default: return "practical";
            }
        }

        types::CascadeSplitMode strToCascadeSplitMode(const std::string& str)
        {
            if (str == "linear") return types::CascadeSplitMode::Linear;
            if (str == "logarithmic") return types::CascadeSplitMode::Logarithmic;
            if (str == "practical") return types::CascadeSplitMode::Practical;
            return types::CascadeSplitMode::Practical;
        }

        // ---- Enum/string conversions for postprocess ----

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

        std::string fxaaQualityToStr(postprocess::FXAAQuality quality)
        {
            switch (quality)
            {
            case postprocess::FXAAQuality::Low: return "low";
            case postprocess::FXAAQuality::Medium: return "medium";
            case postprocess::FXAAQuality::High: return "high";
            default: return "medium";
            }
        }

        postprocess::FXAAQuality strToFXAAQuality(const std::string& str)
        {
            if (str == "low") return postprocess::FXAAQuality::Low;
            if (str == "medium") return postprocess::FXAAQuality::Medium;
            if (str == "high") return postprocess::FXAAQuality::High;
            return postprocess::FXAAQuality::Medium;
        }

        // ---- Serialize post-process helpers ----

        json serializeToneMapping(const postprocess::ToneMappingSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"mode", toneMappingModeToStr(s.mode)},
                {"exposure", s.exposure},
                {"gamma", s.gamma},
                {"contrast", s.contrast}
            };
        }

        json serializeFxaa(const postprocess::FXAASettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"quality", fxaaQualityToStr(s.quality)},
                {"edgeThresholdMin", s.edgeThresholdMin},
                {"edgeThreshold", s.edgeThreshold}
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
                {"maxDistance", s.maxDistance}
            };
        }

        // ---- Deserialize post-process helpers ----

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
        }

        void deserializeFxaa(const json& j, postprocess::FXAASettings& s)
        {
            if (!j.contains("fxaa") || !j["fxaa"].is_object())
                return;
            const auto& fxaa = j["fxaa"];
            if (fxaa.contains("enabled") && fxaa["enabled"].is_boolean())
                s.enabled = fxaa["enabled"].get<bool>();
            if (fxaa.contains("quality") && fxaa["quality"].is_string())
                s.quality = strToFXAAQuality(fxaa["quality"].get<std::string>());
            if (fxaa.contains("edgeThresholdMin") && fxaa["edgeThresholdMin"].is_number())
                s.edgeThresholdMin = std::clamp(fxaa["edgeThresholdMin"].get<float>(), 0.0f, 1.0f);
            if (fxaa.contains("edgeThreshold") && fxaa["edgeThreshold"].is_number())
                s.edgeThreshold = std::clamp(fxaa["edgeThreshold"].get<float>(), 0.0f, 1.0f);
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
        }

        // ---- Deserialize render sub-helpers ----

        void deserializeShadowSettings(const json& j, types::ShadowSettings& settings)
        {
            if (!j.contains("shadows") || !j["shadows"].is_object())
                return;
            const auto& shadows = j["shadows"];
            if (shadows.contains("enabled") && shadows["enabled"].is_boolean())
                settings.enabled = shadows["enabled"].get<bool>();
            if (shadows.contains("quality") && shadows["quality"].is_string())
                settings.quality = strToShadowQuality(shadows["quality"].get<std::string>());
            if (shadows.contains("cascadeCount") && shadows["cascadeCount"].is_number_unsigned())
            {
                uint8_t count = shadows["cascadeCount"].get<uint8_t>();
                settings.cascadeCount = std::clamp(count, uint8_t(1), uint8_t(4));
            }
            if (shadows.contains("cascadeSplitMode") && shadows["cascadeSplitMode"].is_string())
                settings.cascadeSplitMode = strToCascadeSplitMode(shadows["cascadeSplitMode"].get<std::string>());
            if (shadows.contains("shadowBias") && shadows["shadowBias"].is_number())
                settings.shadowBias = shadows["shadowBias"].get<float>();
            if (shadows.contains("slopeBias") && shadows["slopeBias"].is_number())
                settings.slopeBias = shadows["slopeBias"].get<float>();
            if (shadows.contains("normalBias") && shadows["normalBias"].is_number())
                settings.normalBias = shadows["normalBias"].get<float>();
            if (shadows.contains("shadowIntensity") && shadows["shadowIntensity"].is_number())
                settings.shadowIntensity = shadows["shadowIntensity"].get<float>();
        }

        void deserializeCullingSettings(const json& j, types::CullingSettings& settings)
        {
            if (!j.contains("culling") || !j["culling"].is_object())
                return;
            const auto& culling = j["culling"];
            if (culling.contains("frustumCullingEnabled") && culling["frustumCullingEnabled"].is_boolean())
                settings.frustumCullingEnabled = culling["frustumCullingEnabled"].get<bool>();
            if (culling.contains("occlusionCullingEnabled") && culling["occlusionCullingEnabled"].is_boolean())
                settings.occlusionCullingEnabled = culling["occlusionCullingEnabled"].get<bool>();
            if (culling.contains("lodSelectionEnabled") && culling["lodSelectionEnabled"].is_boolean())
                settings.lodSelectionEnabled = culling["lodSelectionEnabled"].get<bool>();
            if (culling.contains("meshletFrustumCullingEnabled") && culling["meshletFrustumCullingEnabled"].is_boolean())
                settings.meshletFrustumCullingEnabled = culling["meshletFrustumCullingEnabled"].get<bool>();
            if (culling.contains("meshletBackfaceCullingEnabled") && culling["meshletBackfaceCullingEnabled"].is_boolean())
                settings.meshletBackfaceCullingEnabled = culling["meshletBackfaceCullingEnabled"].get<bool>();
            if (culling.contains("terrainFrustumCullingEnabled") && culling["terrainFrustumCullingEnabled"].is_boolean())
                settings.terrainFrustumCullingEnabled = culling["terrainFrustumCullingEnabled"].get<bool>();
            if (culling.contains("terrainMeshletCullingEnabled") && culling["terrainMeshletCullingEnabled"].is_boolean())
                settings.terrainMeshletCullingEnabled = culling["terrainMeshletCullingEnabled"].get<bool>();
        }

        void deserializeTerrainRenderSettings(const json& j, types::TerrainSettings& settings)
        {
            if (!j.contains("terrain") || !j["terrain"].is_object())
            {
                settings = types::TerrainSettings{};
                return;
            }
            const auto& terrain = j["terrain"];
            if (terrain.contains("enabled") && terrain["enabled"].is_boolean())
                settings.enabled = terrain["enabled"].get<bool>();
            if (terrain.contains("lodBias") && terrain["lodBias"].is_number())
                settings.lodBias = terrain["lodBias"].get<float>();
            if (terrain.contains("errorThreshold") && terrain["errorThreshold"].is_number())
                settings.errorThreshold = terrain["errorThreshold"].get<float>();
            if (terrain.contains("textureScale") && terrain["textureScale"].is_number())
                settings.textureScale = terrain["textureScale"].get<float>();
            if (terrain.contains("shadowLOD") && terrain["shadowLOD"].is_number_unsigned())
                settings.shadowLOD = std::min(terrain["shadowLOD"].get<uint32_t>(), 3u);
        }
    } // anonymous namespace

    // ---- Render Settings ----

    json SceneSerialization::serializeRenderSettings(const types::RenderSettings& settings)
    {
        json j;

        j["shadows"] = {
            {"enabled", settings.shadows.enabled},
            {"quality", shadowQualityToString(settings.shadows.quality)},
            {"cascadeCount", settings.shadows.cascadeCount},
            {"cascadeSplitMode", cascadeSplitModeToString(settings.shadows.cascadeSplitMode)},
            {"shadowBias", settings.shadows.shadowBias},
            {"slopeBias", settings.shadows.slopeBias},
            {"normalBias", settings.shadows.normalBias},
            {"shadowIntensity", settings.shadows.shadowIntensity}
        };

        j["culling"] = {
            {"frustumCullingEnabled", settings.culling.frustumCullingEnabled},
            {"occlusionCullingEnabled", settings.culling.occlusionCullingEnabled},
            {"lodSelectionEnabled", settings.culling.lodSelectionEnabled},
            {"meshletFrustumCullingEnabled", settings.culling.meshletFrustumCullingEnabled},
            {"meshletBackfaceCullingEnabled", settings.culling.meshletBackfaceCullingEnabled},
            {"terrainFrustumCullingEnabled", settings.culling.terrainFrustumCullingEnabled},
            {"terrainMeshletCullingEnabled", settings.culling.terrainMeshletCullingEnabled}
        };

        j["terrain"] = {
            {"enabled", settings.terrain.enabled},
            {"lodBias", settings.terrain.lodBias},
            {"errorThreshold", settings.terrain.errorThreshold},
            {"textureScale", settings.terrain.textureScale},
            {"shadowLOD", settings.terrain.shadowLOD}
        };

        j["postProcess"] = serializePostProcessSettings(settings.postProcess);

        return j;
    }

    void SceneSerialization::deserializeRenderSettings(const json& j, types::RenderSettings& settings)
    {
        deserializeShadowSettings(j, settings.shadows);
        deserializeCullingSettings(j, settings.culling);
        deserializeTerrainRenderSettings(j, settings.terrain);

        if (j.contains("postProcess") && j["postProcess"].is_object())
        {
            deserializePostProcessSettings(j["postProcess"], settings.postProcess);
        }
        else
        {
            settings.postProcess = postprocess::PostProcessSettings::createDefault();
        }
    }

    // ---- Post-Process Settings ----

    json SceneSerialization::serializePostProcessSettings(const postprocess::PostProcessSettings& settings)
    {
        json j;

        j["enabled"] = settings.enabled;
        j["toneMapping"] = serializeToneMapping(settings.toneMapping);
        j["fxaa"] = serializeFxaa(settings.fxaa);
        j["bloom"] = serializeBloom(settings.bloom);
        j["vignette"] = serializeVignette(settings.vignette);
        j["chromaticAberration"] = serializeChromaticAberration(settings.chromaticAberration);
        j["filmGrain"] = serializeFilmGrain(settings.filmGrain);
        j["depthOfField"] = serializeDepthOfField(settings.depthOfField);
        j["volumetricFog"] = serializeVolumetricFog(settings.volumetricFog);

        return j;
    }

    void SceneSerialization::deserializePostProcessSettings(const json& j, postprocess::PostProcessSettings& settings)
    {
        if (j.contains("enabled") && j["enabled"].is_boolean())
            settings.enabled = j["enabled"].get<bool>();

        deserializeToneMapping(j, settings.toneMapping);
        deserializeFxaa(j, settings.fxaa);
        deserializeBloom(j, settings.bloom);
        deserializeVignette(j, settings.vignette);
        deserializeChromaticAberration(j, settings.chromaticAberration);
        deserializeFilmGrain(j, settings.filmGrain);
        deserializeDepthOfField(j, settings.depthOfField);
        deserializeVolumetricFog(j, settings.volumetricFog);
    }
}
