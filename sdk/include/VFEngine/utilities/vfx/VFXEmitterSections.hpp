#pragma once

#include "VFXTypes.hpp"

#include <array>
#include <string>
#include <string_view>
#include <algorithm>
#include <variant>

// Emitter "module" section registry (editor UX for VFXPropertyPanel).
//
// The emitter property panel shows only "Core" by default; every other section
// (Flipbook, Rendering, Ribbon, ...) is an addable/removable module. Which modules
// are shown is stored per-emitter in VFXNode::enabledSections and persisted in the
// .vfVFX. This header is the single source of truth for the section list + the
// auto-detect migration, shared by the serializer (VFXAsset.cpp) and the editor
// panel so the two never drift.
//
// NOTE: this is UI visibility only. Every emitter property always exists on the
// node with a no-op default, so hiding a section never changes runtime behavior.
namespace vfx
{
    struct EmitterSectionDef
    {
        const char* id;    // stable key stored in VFXNode::enabledSections
        const char* label; // header text shown in the panel + Add-Module list
    };

    // Canonical display order. "Core" is intentionally absent — it is always shown
    // and cannot be removed.
    inline constexpr std::array<EmitterSectionDef, 11> kEmitterSections = {{
        {"spawnVariance", "Spawn Variance"},
        {"flipbook",      "Flipbook"},
        {"rendering",     "Rendering"},
        {"ribbon",        "Ribbon"},
        {"uvScroll",      "UV Scroll"},
        {"bursts",        "Bursts"},
        {"events",        "Events"},
        {"lighting",      "Lighting"},
        {"collision",     "Collision"},
        {"distortion",    "Distortion"},
        {"advanced",      "Advanced"},
    }};

    inline bool sectionEnabled(const VFXNode& node, std::string_view id)
    {
        return std::find(node.enabledSections.begin(), node.enabledSections.end(), id) !=
               node.enabledSections.end();
    }

    namespace sections_detail
    {
        inline int getInt(const VFXNode& n, const char* key, int def)
        {
            auto it = n.properties.find(key);
            if (it != n.properties.end())
                if (const auto* v = std::get_if<int32_t>(&it->second.value)) return *v;
            return def;
        }
        inline float getFloat(const VFXNode& n, const char* key, float def)
        {
            auto it = n.properties.find(key);
            if (it != n.properties.end())
                if (const auto* v = std::get_if<float>(&it->second.value)) return *v;
            return def;
        }
        inline bool getBool(const VFXNode& n, const char* key, bool def)
        {
            auto it = n.properties.find(key);
            if (it != n.properties.end())
                if (const auto* v = std::get_if<bool>(&it->second.value)) return *v;
            return def;
        }
        inline std::string getString(const VFXNode& n, const char* key)
        {
            auto it = n.properties.find(key);
            if (it != n.properties.end())
                if (const auto* v = std::get_if<std::string>(&it->second.value)) return *v;
            return std::string{};
        }
    }

    // True when a section holds any non-default value, i.e. it was configured. Used
    // by the load-time migration to auto-show configured sections of pre-existing
    // .vfVFX assets (which lack the enabledSections key). Float comparisons are
    // against the exact default constant that would have been written, so exact
    // equality is the correct "was this changed" test.
    inline bool sectionHasContent(const VFXNode& n, std::string_view id)
    {
        using namespace sections_detail;
        namespace D = EmitterDefaults;

        if (id == "spawnVariance")
            return getFloat(n, "sizeVariance", D::SIZE_VARIANCE) != D::SIZE_VARIANCE ||
                   getFloat(n, "lifetimeVariance", D::LIFETIME_VARIANCE) != D::LIFETIME_VARIANCE ||
                   getFloat(n, "speedVariance", D::SPEED_VARIANCE) != D::SPEED_VARIANCE ||
                   getFloat(n, "rotationVariance", 0.0f) != 0.0f ||
                   getFloat(n, "angularVelocityVariance", 0.0f) != 0.0f ||
                   getFloat(n, "colorValueVariance", D::COLOR_VALUE_VARIANCE) != D::COLOR_VALUE_VARIANCE ||
                   getFloat(n, "alphaVariance", D::ALPHA_VARIANCE) != D::ALPHA_VARIANCE;
        if (id == "flipbook")
            return getInt(n, "flipbookColumns", D::FLIPBOOK_COLUMNS) > 1 ||
                   getInt(n, "flipbookRows", D::FLIPBOOK_ROWS) > 1 ||
                   getFloat(n, "flipbookFrameRate", D::FLIPBOOK_FRAME_RATE) > 0.0f ||
                   getBool(n, "flipbookRandomStart", D::FLIPBOOK_RANDOM_START) ||
                   getBool(n, "flipbookFrameBlend", D::FLIPBOOK_FRAME_BLEND);
        if (id == "rendering")
            return getInt(n, "renderMode", D::RENDER_MODE) != D::RENDER_MODE ||
                   stringToBlendMode(getString(n, "blendMode")) != VFXBlendMode::Alpha || // VK-1472 (absent -> "" -> Alpha)
                   getBool(n, "additiveBlend", D::ADDITIVE_BLEND) ||
                   getInt(n, "sortOrder", D::SORT_ORDER) != D::SORT_ORDER ||
                   getFloat(n, "softParticleDistance", D::SOFT_PARTICLE_DISTANCE) != D::SOFT_PARTICLE_DISTANCE ||
                   getFloat(n, "stretchMultiplier", D::STRETCH_MULTIPLIER) != D::STRETCH_MULTIPLIER ||
                   getFloat(n, "alphaClipThreshold", D::ALPHA_CLIP_THRESHOLD) != D::ALPHA_CLIP_THRESHOLD ||
                   !getString(n, "meshPath").empty();
        if (id == "ribbon")
            return getInt(n, "renderMode", D::RENDER_MODE) == 4 /* Ribbon */ ||
                   getInt(n, "maxTrailPoints", D::MAX_TRAIL_POINTS) != D::MAX_TRAIL_POINTS ||
                   getFloat(n, "ribbonWidth", D::RIBBON_WIDTH) != D::RIBBON_WIDTH ||
                   getFloat(n, "ribbonMinDistance", D::RIBBON_MIN_DISTANCE) != D::RIBBON_MIN_DISTANCE;
        if (id == "uvScroll")
            return getFloat(n, "uvScrollSpeedU", D::UV_SCROLL_SPEED_U) != D::UV_SCROLL_SPEED_U ||
                   getFloat(n, "uvScrollSpeedV", D::UV_SCROLL_SPEED_V) != D::UV_SCROLL_SPEED_V;
        if (id == "bursts")
            return getInt(n, "burstCount", 0) > 0;
        if (id == "events")
            return getBool(n, "eventOnSpawnEnabled", false) ||
                   getBool(n, "eventOnDeathEnabled", false) ||
                   getBool(n, "eventOnCollisionEnabled", false) ||
                   getBool(n, "eventOnLifetimeThresholdEnabled", false) ||
                   !getString(n, "eventOnSpawnVFX").empty() ||
                   !getString(n, "eventOnDeathVFX").empty() ||
                   !getString(n, "eventOnCollisionVFX").empty() ||
                   !getString(n, "eventOnLifetimeThresholdVFX").empty();
        if (id == "lighting")
            return getFloat(n, "emissiveIntensity", D::EMISSIVE_INTENSITY) != D::EMISSIVE_INTENSITY ||
                   getFloat(n, "lightingInfluence", D::LIGHTING_INFLUENCE) != D::LIGHTING_INFLUENCE ||
                   getFloat(n, "ambientAmount", D::AMBIENT_AMOUNT) != D::AMBIENT_AMOUNT ||
                   getInt(n, "normalMode", D::NORMAL_MODE) != D::NORMAL_MODE ||
                   getBool(n, "lightEmissionEnabled", D::LIGHT_EMISSION_ENABLED);
        if (id == "collision")
            return getBool(n, "collisionEnabled", D::COLLISION_ENABLED);
        if (id == "distortion")
            return getBool(n, "distortionEnabled", false) ||
                   getFloat(n, "distortionStrength", 0.1f) != 0.1f ||
                   !getString(n, "distortionTexture").empty();
        if (id == "advanced")
            return getFloat(n, "inheritVelocityRatio", D::INHERIT_VELOCITY_RATIO) != D::INHERIT_VELOCITY_RATIO ||
                   getBool(n, "lightEmissionEnabled", D::LIGHT_EMISSION_ENABLED) ||
                   getFloat(n, "lightEmissionIntensity", D::LIGHT_EMISSION_INTENSITY) != D::LIGHT_EMISSION_INTENSITY ||
                   getFloat(n, "lightEmissionRadius", D::LIGHT_EMISSION_RADIUS) != D::LIGHT_EMISSION_RADIUS;
        return false;
    }

    // Migration for pre-existing .vfVFX emitters (no enabledSections key): show every
    // section that already holds non-default values, hide the rest.
    inline void autoDetectEnabledSections(VFXNode& node)
    {
        node.enabledSections.clear();
        for (const auto& s : kEmitterSections)
            if (sectionHasContent(node, s.id))
                node.enabledSections.emplace_back(s.id);
    }
}
