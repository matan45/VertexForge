#pragma once

// Shared JSON (de)serialization for vegetation::ScatterProfile (VK-1585).
//
// A single home for the scatter rule/profile JSON shape so it is written in exactly one
// place and cannot drift. Used by BOTH the scene-inline form (SceneSerializeGrass.cpp) and
// the standalone .vfScatterProfile asset (ScatterProfileAsset.cpp). Field names + order match
// the original VK-1581 scene-inline layout, so old scenes round-trip unchanged; the curvature
// keys are VK-1585 additions and reads are missing-key tolerant (no scene-format bump).
//
// Header-only + inline (nlohmann/json is header-only, the functions are pure) so it links from
// the Serialization DLL, Utilities, Editor, and Tests with no cross-module link dependency.

#include "VegetationScatterTypes.hpp"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <fstream>
#include <string>
#include <type_traits>

namespace vegetation
{
    namespace detail
    {
        template <typename T>
        inline void readScatterField(const nlohmann::json& j, const char* key, T& out)
        {
            auto it = j.find(key);
            if (it == j.end())
                return;
            // Type-guard so a present-but-wrong-typed value keeps the struct default instead of
            // throwing json::type_error out of the deserializer (matches the is_*()-guarded sibling
            // reads and the documented "missing/bad key keeps default" contract). bool is checked
            // before is_arithmetic (bool is arithmetic) so it uses is_boolean().
            if constexpr (std::is_same_v<T, bool>)
            {
                if (it->is_boolean()) out = it->get<bool>();
            }
            else if constexpr (std::is_arithmetic_v<T>)
            {
                if (it->is_number()) out = it->get<T>();
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                if (it->is_string()) out = it->get<std::string>();
            }
            else
            {
                try { out = it->get<T>(); } catch (...) {}
            }
        }
    }

    // Serialize one ScatterRule into a JSON object.
    inline void serializeScatterRule(nlohmann::json& rj, const ScatterRule& r)
    {
        rj["paletteEntryIndex"] = r.paletteEntryIndex;
        rj["density"] = r.density;
        rj["spacing"] = r.spacing;
        rj["positionJitter"] = r.positionJitter;
        rj["alignToNormal"] = r.alignToNormal;
        rj["useSlopeMask"] = r.useSlopeMask;
        rj["slopeMinCos"] = r.slopeMinCos;
        rj["slopeMaxCos"] = r.slopeMaxCos;
        rj["useHeightMask"] = r.useHeightMask;
        rj["heightMin"] = r.heightMin;
        rj["heightMax"] = r.heightMax;
        rj["useNoiseMask"] = r.useNoiseMask;
        rj["noiseFrequency"] = r.noiseFrequency;
        rj["noiseThreshold"] = r.noiseThreshold;
        rj["noiseSeed"] = r.noiseSeed;
        rj["useLayerMask"] = r.useLayerMask;
        rj["layerIndex"] = static_cast<int>(r.layerIndex);
        rj["layerWeightMin"] = r.layerWeightMin;
        rj["invertLayer"] = r.invertLayer;
        rj["useCurvatureMask"] = r.useCurvatureMask;
        rj["curvatureMin"] = r.curvatureMin;
        rj["curvatureMax"] = r.curvatureMax;
    }

    // Read one ScatterRule from a JSON object (missing keys keep struct defaults).
    inline void deserializeScatterRule(const nlohmann::json& rj, ScatterRule& r)
    {
        detail::readScatterField(rj, "paletteEntryIndex", r.paletteEntryIndex);
        detail::readScatterField(rj, "density", r.density);
        detail::readScatterField(rj, "spacing", r.spacing);
        detail::readScatterField(rj, "positionJitter", r.positionJitter);
        detail::readScatterField(rj, "alignToNormal", r.alignToNormal);
        detail::readScatterField(rj, "useSlopeMask", r.useSlopeMask);
        detail::readScatterField(rj, "slopeMinCos", r.slopeMinCos);
        detail::readScatterField(rj, "slopeMaxCos", r.slopeMaxCos);
        detail::readScatterField(rj, "useHeightMask", r.useHeightMask);
        detail::readScatterField(rj, "heightMin", r.heightMin);
        detail::readScatterField(rj, "heightMax", r.heightMax);
        detail::readScatterField(rj, "useNoiseMask", r.useNoiseMask);
        detail::readScatterField(rj, "noiseFrequency", r.noiseFrequency);
        detail::readScatterField(rj, "noiseThreshold", r.noiseThreshold);
        detail::readScatterField(rj, "noiseSeed", r.noiseSeed);
        detail::readScatterField(rj, "useLayerMask", r.useLayerMask);
        if (auto it = rj.find("layerIndex"); it != rj.end() && it->is_number())
            r.layerIndex = static_cast<uint8_t>(it->get<int>());
        detail::readScatterField(rj, "layerWeightMin", r.layerWeightMin);
        detail::readScatterField(rj, "invertLayer", r.invertLayer);
        detail::readScatterField(rj, "useCurvatureMask", r.useCurvatureMask);
        detail::readScatterField(rj, "curvatureMin", r.curvatureMin);
        detail::readScatterField(rj, "curvatureMax", r.curvatureMax);
    }

    // Serialize a whole ScatterProfile into `out` (the scatter object itself, not the parent).
    inline void serializeScatterProfile(nlohmann::json& out, const ScatterProfile& p)
    {
        out["globalSeed"] = p.globalSeed;
        out["globalDensityScale"] = p.globalDensityScale;
        out["domain"] = static_cast<int>(p.domain);
        nlohmann::json rulesArr = nlohmann::json::array();
        for (const auto& r : p.rules)
        {
            nlohmann::json rj;
            serializeScatterRule(rj, r);
            rulesArr.push_back(rj);
        }
        out["rules"] = rulesArr;

        // Biomes (VK-1585). Absent/empty ⇒ legacy flat-rules-only behaviour on read.
        nlohmann::json biomesArr = nlohmann::json::array();
        for (const auto& b : p.biomes)
        {
            nlohmann::json bj;
            bj["name"] = b.name;
            bj["biomeLayerIndex"] = static_cast<int>(b.biomeLayerIndex);
            bj["priority"] = b.priority;
            bj["edgeBlendWidth"] = b.edgeBlendWidth;
            bj["densityScale"] = b.densityScale;
            nlohmann::json bRules = nlohmann::json::array();
            for (const auto& r : b.rules)
            {
                nlohmann::json rj;
                serializeScatterRule(rj, r);
                bRules.push_back(rj);
            }
            bj["rules"] = bRules;
            biomesArr.push_back(bj);
        }
        out["biomes"] = biomesArr;
    }

    // Read a whole ScatterProfile from `in` (the scatter object). Absent keys keep defaults.
    inline void deserializeScatterProfile(const nlohmann::json& in, ScatterProfile& p)
    {
        detail::readScatterField(in, "globalSeed", p.globalSeed);
        detail::readScatterField(in, "globalDensityScale", p.globalDensityScale);
        if (auto dit = in.find("domain"); dit != in.end() && dit->is_number())
            p.domain = static_cast<ScatterDomain>(dit->get<int>());
        if (auto it = in.find("rules"); it != in.end() && it->is_array())
        {
            p.rules.clear();
            for (const auto& rj : *it)
            {
                ScatterRule r;
                deserializeScatterRule(rj, r);
                p.rules.push_back(r);
            }
        }

        // Biomes (VK-1585). Absent ⇒ p.biomes stays empty (legacy behaviour).
        if (auto bit = in.find("biomes"); bit != in.end() && bit->is_array())
        {
            p.biomes.clear();
            for (const auto& bj : *bit)
            {
                BiomeLayer b;
                detail::readScatterField(bj, "name", b.name);
                if (auto lit = bj.find("biomeLayerIndex"); lit != bj.end() && lit->is_number())
                    b.biomeLayerIndex = static_cast<uint8_t>(lit->get<int>());
                detail::readScatterField(bj, "priority", b.priority);
                detail::readScatterField(bj, "edgeBlendWidth", b.edgeBlendWidth);
                detail::readScatterField(bj, "densityScale", b.densityScale);
                if (auto rit = bj.find("rules"); rit != bj.end() && rit->is_array())
                    for (const auto& rj : *rit)
                    {
                        ScatterRule r;
                        deserializeScatterRule(rj, r);
                        b.rules.push_back(r);
                    }
                p.biomes.push_back(b);
            }
        }
    }

    // File format version for a standalone scatter profile (.vfScatterProfile / foliage_scatter.json).
    inline constexpr int kScatterProfileFileVersion = 1;

    // Save/load a standalone scatter profile file: a flat object of { "version", ...profile }.
    // Shared by the .vfScatterProfile asset (VK-1585) and the per-terrain foliage-scatter sidecar.
    // Reads are missing-key tolerant, so older/newer files load with defaults for absent fields.
    inline bool saveScatterProfileFile(const std::string& path, const ScatterProfile& p)
    {
        nlohmann::json j;
        j["version"] = kScatterProfileFileVersion;
        serializeScatterProfile(j, p);
        std::ofstream f(path);
        if (!f.is_open())
            return false;
        f << j.dump(2);
        return f.good();
    }

    inline bool loadScatterProfileFile(const std::string& path, ScatterProfile& p)
    {
        std::ifstream f(path);
        if (!f.is_open())
            return false;
        nlohmann::json j;
        try
        {
            f >> j;
            if (!j.is_object())
                return false;
            // Inside the try: deserializeScatterProfile is now type-tolerant (readScatterField),
            // but keep it guarded so any residual throw honors the bool return contract rather than
            // escaping to callers (e.g. SceneSerializeGrass::deserializeGrass) and aborting load.
            deserializeScatterProfile(j, p);
        }
        catch (...)
        {
            return false;
        }
        return true;
    }
}
