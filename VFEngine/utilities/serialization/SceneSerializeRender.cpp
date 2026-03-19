#include "SceneSerialization.hpp"
#include "JsonConverters.hpp"
#include "../components/Components.hpp"
#include <algorithm>

namespace serialization
{
    namespace
    {
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

        json serializeShadowSettings(const types::ShadowSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"quality", shadowQualityToStr(s.quality)},
                {"cascadeCount", s.cascadeCount},
                {"cascadeSplitMode", cascadeSplitModeToStr(s.cascadeSplitMode)},
                {"shadowBias", s.shadowBias},
                {"slopeBias", s.slopeBias},
                {"normalBias", s.normalBias},
                {"softShadows", s.softShadows},
                {"shadowIntensity", s.shadowIntensity},
                {"directionalResolution", s.directionalResolution},
                {"spotResolution", s.spotResolution},
                {"pointResolution", s.pointResolution}
            };
        }

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
            if (shadows.contains("softShadows") && shadows["softShadows"].is_boolean())
                settings.softShadows = shadows["softShadows"].get<bool>();
            if (shadows.contains("shadowIntensity") && shadows["shadowIntensity"].is_number())
                settings.shadowIntensity = shadows["shadowIntensity"].get<float>();
            if (shadows.contains("directionalResolution") && shadows["directionalResolution"].is_number_unsigned())
                settings.directionalResolution = shadows["directionalResolution"].get<uint32_t>();
            if (shadows.contains("spotResolution") && shadows["spotResolution"].is_number_unsigned())
                settings.spotResolution = shadows["spotResolution"].get<uint32_t>();
            if (shadows.contains("pointResolution") && shadows["pointResolution"].is_number_unsigned())
                settings.pointResolution = shadows["pointResolution"].get<uint32_t>();
        }

        json serializeCullingSettings(const types::CullingSettings& s)
        {
            return {
                {"frustumCullingEnabled", s.frustumCullingEnabled},
                {"occlusionCullingEnabled", s.occlusionCullingEnabled},
                {"lodSelectionEnabled", s.lodSelectionEnabled},
                {"meshletFrustumCullingEnabled", s.meshletFrustumCullingEnabled},
                {"meshletBackfaceCullingEnabled", s.meshletBackfaceCullingEnabled},
                {"terrainFrustumCullingEnabled", s.terrainFrustumCullingEnabled},
                {"terrainMeshletCullingEnabled", s.terrainMeshletCullingEnabled},
                {"lodCrossfadeEnabled", s.lodCrossfadeEnabled},
                {"globalLodBias", s.globalLodBias}
            };
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
            if (culling.contains("lodCrossfadeEnabled") && culling["lodCrossfadeEnabled"].is_boolean())
                settings.lodCrossfadeEnabled = culling["lodCrossfadeEnabled"].get<bool>();
            if (culling.contains("globalLodBias") && culling["globalLodBias"].is_number())
                settings.globalLodBias = culling["globalLodBias"].get<float>();
        }

        void deserializeTransparencySettings(const json& j, types::TransparencySettings& settings)
        {
            // Support loading from new "transparency" section
            if (j.contains("transparency") && j["transparency"].is_object())
            {
                const auto& transparency = j["transparency"];
                if (transparency.contains("wboitEnabled") && transparency["wboitEnabled"].is_boolean())
                    settings.wboitEnabled = transparency["wboitEnabled"].get<bool>();
                return;
            }
            // Backward compatibility: load from old "culling" section
            if (j.contains("culling") && j["culling"].is_object())
            {
                const auto& culling = j["culling"];
                if (culling.contains("wboitEnabled") && culling["wboitEnabled"].is_boolean())
                    settings.wboitEnabled = culling["wboitEnabled"].get<bool>();
            }
        }

        json serializeTerrainRenderSettings(const types::TerrainSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"lodBias", s.lodBias},
                {"errorThreshold", s.errorThreshold},
                {"textureScale", s.textureScale},
                {"shadowLOD", s.shadowLOD}
            };
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
        json serializeDistanceCullingSettings(const types::DistanceCullingSettings& s)
        {
            return {
                {"enabled", s.enabled},
                {"staticMeshDistance", s.staticMeshDistance},
                {"terrainDistance", s.terrainDistance},
                {"foliageDistance", s.foliageDistance},
                {"vfxDistance", s.vfxDistance},
                {"decalDistance", s.decalDistance},
                {"billboardDistance", s.billboardDistance},
                {"waterDistance", s.waterDistance},
                {"shadowDistanceMultiplier", s.shadowDistanceMultiplier}
            };
        }

        void deserializeDistanceCullingSettings(const json& j, types::DistanceCullingSettings& settings)
        {
            if (!j.contains("distanceCulling") || !j["distanceCulling"].is_object())
            {
                settings = types::DistanceCullingSettings{};
                return;
            }
            const auto& dc = j["distanceCulling"];
            if (dc.contains("enabled") && dc["enabled"].is_boolean())
                settings.enabled = dc["enabled"].get<bool>();
            if (dc.contains("staticMeshDistance") && dc["staticMeshDistance"].is_number())
                settings.staticMeshDistance = dc["staticMeshDistance"].get<float>();
            if (dc.contains("terrainDistance") && dc["terrainDistance"].is_number())
                settings.terrainDistance = dc["terrainDistance"].get<float>();
            if (dc.contains("foliageDistance") && dc["foliageDistance"].is_number())
                settings.foliageDistance = dc["foliageDistance"].get<float>();
            if (dc.contains("vfxDistance") && dc["vfxDistance"].is_number())
                settings.vfxDistance = dc["vfxDistance"].get<float>();
            if (dc.contains("decalDistance") && dc["decalDistance"].is_number())
                settings.decalDistance = dc["decalDistance"].get<float>();
            if (dc.contains("billboardDistance") && dc["billboardDistance"].is_number())
                settings.billboardDistance = dc["billboardDistance"].get<float>();
            if (dc.contains("waterDistance") && dc["waterDistance"].is_number())
                settings.waterDistance = dc["waterDistance"].get<float>();
            if (dc.contains("shadowDistanceMultiplier") && dc["shadowDistanceMultiplier"].is_number())
                settings.shadowDistanceMultiplier = dc["shadowDistanceMultiplier"].get<float>();
        }
        // ---- GI helpers ----

        json serializeGISettings(const render::gi::GISettings& gi)
        {
            return {
                {"enabled", gi.enabled},
                {"quality", static_cast<int>(gi.quality)},
                {"probeSpacing", gi.probeSpacing},
                {"cascadeMultiplier", gi.cascadeMultiplier},
                {"probeRaysPerUpdate", gi.probeRaysPerUpdate},
                {"temporalBlendFactor", gi.temporalBlendFactor},
                {"probeUpdateRate", gi.probeUpdateRate},
                {"maxProbeDistance", gi.maxProbeDistance},
                {"farFieldEnabled", gi.farFieldEnabled},
                {"farFieldMaxDistance", gi.farFieldMaxDistance},
                {"farFieldCascadeCount", gi.farFieldCascadeCount},
                {"farFieldProbeSpacing", gi.farFieldProbeSpacing},
                {"farFieldRaysPerUpdate", gi.farFieldRaysPerUpdate},
                {"farFieldUpdateRate", gi.farFieldUpdateRate},
                {"ssgiEnabled", gi.ssgiEnabled},
                {"ssgiIntensity", gi.ssgiIntensity},
                {"ssgiRadius", gi.ssgiRadius},
                {"ssgiMaxDistance", gi.ssgiMaxDistance},
                {"ssgiSampleCount", gi.ssgiSampleCount},
                {"ssgiTemporalBlend", gi.ssgiTemporalBlend},
                {"ssgiHalfResolution", gi.ssgiHalfResolution}
            };
        }

        void deserializeGISettings(const json& j, render::gi::GISettings& gi)
        {
            if (!j.contains("gi") || !j["gi"].is_object())
                return;
            const auto& g = j["gi"];
            if (g.contains("enabled") && g["enabled"].is_boolean())
                gi.enabled = g["enabled"].get<bool>();
            if (g.contains("quality") && g["quality"].is_number_integer())
                gi.quality = static_cast<render::gi::GIQuality>(std::clamp(g["quality"].get<int>(), 0, 3));
            if (g.contains("probeSpacing") && g["probeSpacing"].is_number())
                gi.probeSpacing = g["probeSpacing"].get<float>();
            if (g.contains("cascadeMultiplier") && g["cascadeMultiplier"].is_number())
                gi.cascadeMultiplier = g["cascadeMultiplier"].get<float>();
            if (g.contains("probeRaysPerUpdate") && g["probeRaysPerUpdate"].is_number_unsigned())
                gi.probeRaysPerUpdate = g["probeRaysPerUpdate"].get<uint32_t>();
            if (g.contains("temporalBlendFactor") && g["temporalBlendFactor"].is_number())
                gi.temporalBlendFactor = g["temporalBlendFactor"].get<float>();
            if (g.contains("probeUpdateRate") && g["probeUpdateRate"].is_number())
                gi.probeUpdateRate = g["probeUpdateRate"].get<float>();
            if (g.contains("maxProbeDistance") && g["maxProbeDistance"].is_number())
                gi.maxProbeDistance = g["maxProbeDistance"].get<float>();
            if (g.contains("farFieldEnabled") && g["farFieldEnabled"].is_boolean())
                gi.farFieldEnabled = g["farFieldEnabled"].get<bool>();
            if (g.contains("farFieldMaxDistance") && g["farFieldMaxDistance"].is_number())
                gi.farFieldMaxDistance = g["farFieldMaxDistance"].get<float>();
            if (g.contains("farFieldCascadeCount") && g["farFieldCascadeCount"].is_number_unsigned())
                gi.farFieldCascadeCount = g["farFieldCascadeCount"].get<uint32_t>();
            if (g.contains("farFieldProbeSpacing") && g["farFieldProbeSpacing"].is_number())
                gi.farFieldProbeSpacing = g["farFieldProbeSpacing"].get<float>();
            if (g.contains("farFieldRaysPerUpdate") && g["farFieldRaysPerUpdate"].is_number_unsigned())
                gi.farFieldRaysPerUpdate = g["farFieldRaysPerUpdate"].get<uint32_t>();
            if (g.contains("farFieldUpdateRate") && g["farFieldUpdateRate"].is_number())
                gi.farFieldUpdateRate = g["farFieldUpdateRate"].get<float>();
            if (g.contains("ssgiEnabled") && g["ssgiEnabled"].is_boolean())
                gi.ssgiEnabled = g["ssgiEnabled"].get<bool>();
            if (g.contains("ssgiIntensity") && g["ssgiIntensity"].is_number())
                gi.ssgiIntensity = g["ssgiIntensity"].get<float>();
            if (g.contains("ssgiRadius") && g["ssgiRadius"].is_number())
                gi.ssgiRadius = g["ssgiRadius"].get<float>();
            if (g.contains("ssgiMaxDistance") && g["ssgiMaxDistance"].is_number())
                gi.ssgiMaxDistance = g["ssgiMaxDistance"].get<float>();
            if (g.contains("ssgiSampleCount") && g["ssgiSampleCount"].is_number_integer())
                gi.ssgiSampleCount = std::clamp(g["ssgiSampleCount"].get<int>(), 4, 16);
            if (g.contains("ssgiTemporalBlend") && g["ssgiTemporalBlend"].is_number())
                gi.ssgiTemporalBlend = g["ssgiTemporalBlend"].get<float>();
            if (g.contains("ssgiHalfResolution") && g["ssgiHalfResolution"].is_boolean())
                gi.ssgiHalfResolution = g["ssgiHalfResolution"].get<bool>();
        }

        // ---- VFX LOD helpers ----

        json serializeVfxLODSettings(const types::VFXLODSettings& vfxLOD)
        {
            return {
                {"lod0Distance", vfxLOD.lod0Distance},
                {"lod1Distance", vfxLOD.lod1Distance},
                {"lod2Distance", vfxLOD.lod2Distance},
                {"transitionZone", vfxLOD.transitionZone}
            };
        }

        void deserializeVfxLODSettings(const json& j, types::VFXLODSettings& vfxLOD)
        {
            if (!j.contains("vfxLOD") || !j["vfxLOD"].is_object())
                return;
            const auto& vl = j["vfxLOD"];
            if (vl.contains("lod0Distance") && vl["lod0Distance"].is_number())
                vfxLOD.lod0Distance = vl["lod0Distance"].get<float>();
            if (vl.contains("lod1Distance") && vl["lod1Distance"].is_number())
                vfxLOD.lod1Distance = vl["lod1Distance"].get<float>();
            if (vl.contains("lod2Distance") && vl["lod2Distance"].is_number())
                vfxLOD.lod2Distance = vl["lod2Distance"].get<float>();
            if (vl.contains("transitionZone") && vl["transitionZone"].is_number())
                vfxLOD.transitionZone = vl["transitionZone"].get<float>();
        }

        // ---- Animation LOD helpers ----

        json serializeAnimationLODSettings(const types::AnimationLODSettings& animLOD)
        {
            return {
                {"lod0Distance", animLOD.lod0Distance},
                {"lod1Distance", animLOD.lod1Distance},
                {"lod2Distance", animLOD.lod2Distance},
                {"lod3Distance", animLOD.lod3Distance},
                {"lod0Interval", animLOD.lod0Interval},
                {"lod1Interval", animLOD.lod1Interval},
                {"lod2Interval", animLOD.lod2Interval},
                {"maxStreamingInitPerFrame", animLOD.maxStreamingInitPerFrame}
            };
        }

        void deserializeAnimationLODSettings(const json& j, types::AnimationLODSettings& animLOD)
        {
            if (!j.contains("animationLOD") || !j["animationLOD"].is_object())
                return;
            const auto& al = j["animationLOD"];
            if (al.contains("lod0Distance") && al["lod0Distance"].is_number())
                animLOD.lod0Distance = al["lod0Distance"].get<float>();
            if (al.contains("lod1Distance") && al["lod1Distance"].is_number())
                animLOD.lod1Distance = al["lod1Distance"].get<float>();
            if (al.contains("lod2Distance") && al["lod2Distance"].is_number())
                animLOD.lod2Distance = al["lod2Distance"].get<float>();
            if (al.contains("lod3Distance") && al["lod3Distance"].is_number())
                animLOD.lod3Distance = al["lod3Distance"].get<float>();
            if (al.contains("lod0Interval") && al["lod0Interval"].is_number_unsigned())
                animLOD.lod0Interval = al["lod0Interval"].get<uint32_t>();
            if (al.contains("lod1Interval") && al["lod1Interval"].is_number_unsigned())
                animLOD.lod1Interval = al["lod1Interval"].get<uint32_t>();
            if (al.contains("lod2Interval") && al["lod2Interval"].is_number_unsigned())
                animLOD.lod2Interval = al["lod2Interval"].get<uint32_t>();
            if (al.contains("maxStreamingInitPerFrame") && al["maxStreamingInitPerFrame"].is_number_unsigned())
                animLOD.maxStreamingInitPerFrame = al["maxStreamingInitPerFrame"].get<uint32_t>();
        }

    } // anonymous namespace

    // ---- Render Settings ----

    json SceneSerialization::serializeRenderSettings(const types::RenderSettings& settings)
    {
        json j;

        j["shadows"] = serializeShadowSettings(settings.shadows);
        j["culling"] = serializeCullingSettings(settings.culling);
        j["distanceCulling"] = serializeDistanceCullingSettings(settings.distanceCulling);
        j["transparency"] = { {"wboitEnabled", settings.transparency.wboitEnabled} };
        j["shadowLOD"] = { {"enabled", settings.shadowLOD.enabled} };
        j["terrain"] = serializeTerrainRenderSettings(settings.terrain);
        j["postProcess"] = serializePostProcessSettings(settings.postProcess);
        j["gi"] = serializeGISettings(settings.gi);
        j["vfxLOD"] = serializeVfxLODSettings(settings.vfxLOD);
        j["animationLOD"] = serializeAnimationLODSettings(settings.animationLOD);
        j["atmosphere"] = serializeAtmosphereSettings(settings.atmosphere);
        j["cloud"] = serializeCloudSettings(settings.cloud);

        return j;
    }

    void SceneSerialization::deserializeRenderSettings(const json& j, types::RenderSettings& settings)
    {
        deserializeShadowSettings(j, settings.shadows);

        if (j.contains("shadowLOD") && j["shadowLOD"].is_object())
        {
            const auto& sl = j["shadowLOD"];
            if (sl.contains("enabled") && sl["enabled"].is_boolean())
                settings.shadowLOD.enabled = sl["enabled"].get<bool>();
        }

        deserializeCullingSettings(j, settings.culling);
        deserializeDistanceCullingSettings(j, settings.distanceCulling);
        deserializeTransparencySettings(j, settings.transparency);
        deserializeTerrainRenderSettings(j, settings.terrain);

        deserializeGISettings(j, settings.gi);

        if (j.contains("postProcess") && j["postProcess"].is_object())
        {
            deserializePostProcessSettings(j["postProcess"], settings.postProcess);
        }
        else
        {
            settings.postProcess = postprocess::PostProcessSettings::createDefault();
        }

        deserializeVfxLODSettings(j, settings.vfxLOD);
        deserializeAnimationLODSettings(j, settings.animationLOD);

        deserializeAtmosphereSettings(j, settings.atmosphere);
        deserializeCloudSettings(j, settings.cloud);
    }
}
