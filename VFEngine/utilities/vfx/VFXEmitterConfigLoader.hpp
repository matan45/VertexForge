#pragma once

#include "VFXTypes.hpp"
#include "VFXAsset.hpp"
#include "VFXModifierConfigLoader.hpp"
#include "VFXForceConfigLoader.hpp"
#include "VFXShapeConfigLoader.hpp"
#include "VFXBurstTypes.hpp"
#include <algorithm>
#include <optional>
#include <string_view>

namespace vfx
{
    class VFXEmitterConfigLoader
    {
    public:
        static std::optional<render::vfx::VFXEmitterConfig> loadFromFile(std::string_view path);
        static render::vfx::VFXEmitterConfig fromVFXData(const VFXData& data);

    private:
        template<typename T>
        static T getPropertyValue(const VFXNode& node, const std::string& propName, T defaultValue);

        static float getFloat(const VFXNode& node, const std::string& propName, float defaultValue);
        static int32_t getInt(const VFXNode& node, const std::string& propName, int32_t defaultValue);
        static glm::vec3 getVec3(const VFXNode& node, const std::string& propName, const glm::vec3& defaultValue);
        static glm::vec4 getVec4(const VFXNode& node, const std::string& propName, const glm::vec4& defaultValue);
        static bool getBool(const VFXNode& node, const std::string& propName, bool defaultValue);
        static std::string getString(const VFXNode& node, const std::string& propName, const std::string& defaultValue);
    };

    template<typename T>
    T VFXEmitterConfigLoader::getPropertyValue(const VFXNode& node, const std::string& propName, T defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
        {
            return defaultValue;
        }

        const auto& prop = it->second;
        if (auto* val = std::get_if<T>(&prop.value))
        {
            return *val;
        }

        return defaultValue;
    }

    inline float VFXEmitterConfigLoader::getFloat(const VFXNode& node, const std::string& propName, float defaultValue)
    {
        return getPropertyValue<float>(node, propName, defaultValue);
    }

    inline int32_t VFXEmitterConfigLoader::getInt(const VFXNode& node, const std::string& propName, int32_t defaultValue)
    {
        return getPropertyValue<int32_t>(node, propName, defaultValue);
    }

    inline glm::vec3 VFXEmitterConfigLoader::getVec3(const VFXNode& node, const std::string& propName, const glm::vec3& defaultValue)
    {
        return getPropertyValue<glm::vec3>(node, propName, defaultValue);
    }

    inline glm::vec4 VFXEmitterConfigLoader::getVec4(const VFXNode& node, const std::string& propName, const glm::vec4& defaultValue)
    {
        return getPropertyValue<glm::vec4>(node, propName, defaultValue);
    }

    inline bool VFXEmitterConfigLoader::getBool(const VFXNode& node, const std::string& propName, bool defaultValue)
    {
        return getPropertyValue<bool>(node, propName, defaultValue);
    }

    inline std::string VFXEmitterConfigLoader::getString(const VFXNode& node, const std::string& propName, const std::string& defaultValue)
    {
        return getPropertyValue<std::string>(node, propName, defaultValue);
    }

    inline std::optional<render::vfx::VFXEmitterConfig> VFXEmitterConfigLoader::loadFromFile(std::string_view path)
    {
        auto vfxData = VFXAsset::load(path);
        if (!vfxData.has_value())
        {
            return std::nullopt;
        }

        return fromVFXData(vfxData.value());
    }

    inline render::vfx::VFXEmitterConfig VFXEmitterConfigLoader::fromVFXData(const VFXData& data)
    {
        render::vfx::VFXEmitterConfig config;

        const VFXNode* emitterNode = data.graph.findEmitterNode();
        if (emitterNode == nullptr)
        {
            return config;
        }

        config.spawnRate = getFloat(*emitterNode, "spawnRate", EmitterDefaults::SPAWN_RATE);
        config.lifetime = getFloat(*emitterNode, "lifetime", EmitterDefaults::LIFETIME);
        config.startSize = getFloat(*emitterNode, "startSize", EmitterDefaults::START_SIZE);
        config.startSpeed = getFloat(*emitterNode, "startSpeed", EmitterDefaults::START_SPEED);
        config.emitDirection = getVec3(*emitterNode, "startVelocity", glm::vec3(0.0f, 1.0f, 0.0f));
        config.startColor = getVec4(*emitterNode, "startColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        config.looping = getBool(*emitterNode, "looping", EmitterDefaults::LOOPING);
        config.texturePath = getString(*emitterNode, "texture", "");
        config.inheritVelocityRatio = std::clamp(
            getFloat(*emitterNode, "inheritVelocityRatio", EmitterDefaults::INHERIT_VELOCITY_RATIO), 0.0f, 1.0f);
        config.sizeVariance = std::clamp(
            getFloat(*emitterNode, "sizeVariance", EmitterDefaults::SIZE_VARIANCE), 0.0f, 1.0f);
        config.lifetimeVariance = std::clamp(
            getFloat(*emitterNode, "lifetimeVariance", EmitterDefaults::LIFETIME_VARIANCE), 0.0f, 1.0f);
        config.speedVariance = std::clamp(
            getFloat(*emitterNode, "speedVariance", EmitterDefaults::SPEED_VARIANCE), 0.0f, 1.0f);
        config.rotationVariance = glm::radians(std::max(0.0f,
            getFloat(*emitterNode, "rotationVariance", EmitterDefaults::ROTATION_VARIANCE_DEGREES)));
        config.angularVelocityVariance = glm::radians(std::max(0.0f,
            getFloat(*emitterNode, "angularVelocityVariance", EmitterDefaults::ANGULAR_VELOCITY_VARIANCE_DEGREES)));
        config.colorValueVariance = std::clamp(
            getFloat(*emitterNode, "colorValueVariance", EmitterDefaults::COLOR_VALUE_VARIANCE), 0.0f, 1.0f);
        config.alphaVariance = std::clamp(
            getFloat(*emitterNode, "alphaVariance", EmitterDefaults::ALPHA_VARIANCE), 0.0f, 1.0f);

        config.modifiers = VFXModifierConfigLoader::fromGraph(data.graph);
        config.forces = VFXForceConfigLoader::fromGraph(data.graph);
        config.shape = VFXShapeConfigLoader::fromGraph(data.graph);
        config.bursts = loadBurstsFromNode(*emitterNode);

        config.flipbookRows = std::clamp(getInt(*emitterNode, "flipbookRows", EmitterDefaults::FLIPBOOK_ROWS), 1, 16);
        config.flipbookColumns = std::clamp(getInt(*emitterNode, "flipbookColumns", EmitterDefaults::FLIPBOOK_COLUMNS), 1, 16);
        config.flipbookFrameRate = getFloat(*emitterNode, "flipbookFrameRate", EmitterDefaults::FLIPBOOK_FRAME_RATE);
        config.flipbookRandomStart = getBool(*emitterNode, "flipbookRandomStart", EmitterDefaults::FLIPBOOK_RANDOM_START);
        config.flipbookFrameBlend = getBool(*emitterNode, "flipbookFrameBlend", EmitterDefaults::FLIPBOOK_FRAME_BLEND);

        config.alphaClipThreshold = getFloat(*emitterNode, "alphaClipThreshold", EmitterDefaults::ALPHA_CLIP_THRESHOLD);
        config.additiveBlend = getBool(*emitterNode, "additiveBlend", EmitterDefaults::ADDITIVE_BLEND);

        config.renderMode = static_cast<render::vfx::VFXRenderMode>(
            std::clamp(getInt(*emitterNode, "renderMode", EmitterDefaults::RENDER_MODE), 0, 4));
        config.softParticleDistance = std::max(0.0f,
            getFloat(*emitterNode, "softParticleDistance", EmitterDefaults::SOFT_PARTICLE_DISTANCE));
        config.stretchMultiplier = std::max(0.1f,
            getFloat(*emitterNode, "stretchMultiplier", EmitterDefaults::STRETCH_MULTIPLIER));

        config.meshPath = getString(*emitterNode, "meshPath", "");

        config.maxTrailPoints = static_cast<uint32_t>(
            std::clamp(getInt(*emitterNode, "maxTrailPoints", EmitterDefaults::MAX_TRAIL_POINTS), 2, 256));
        config.ribbonWidth = std::max(0.01f,
            getFloat(*emitterNode, "ribbonWidth", EmitterDefaults::RIBBON_WIDTH));
        config.ribbonMinDistance = std::max(0.0f,
            getFloat(*emitterNode, "ribbonMinDistance", EmitterDefaults::RIBBON_MIN_DISTANCE));

        config.uvScrollSpeedU = getFloat(*emitterNode, "uvScrollSpeedU", EmitterDefaults::UV_SCROLL_SPEED_U);
        config.uvScrollSpeedV = getFloat(*emitterNode, "uvScrollSpeedV", EmitterDefaults::UV_SCROLL_SPEED_V);

        // Glow color
        config.glowColor = VFXModifierConfigLoader::getGlowColorFromChain(config.modifiers);
        config.emissiveIntensity = std::max(0.0f,
            getFloat(*emitterNode, "emissiveIntensity", EmitterDefaults::EMISSIVE_INTENSITY));

        // Events
        config.events.onSpawnEnabled = getBool(*emitterNode, "eventOnSpawnEnabled", false);
        config.events.onSpawnVFXPath = getString(*emitterNode, "eventOnSpawnVFX", "");
        config.events.onDeathEnabled = getBool(*emitterNode, "eventOnDeathEnabled", false);
        config.events.onDeathVFXPath = getString(*emitterNode, "eventOnDeathVFX", "");
        config.events.onCollisionEnabled = getBool(*emitterNode, "eventOnCollisionEnabled", false);
        config.events.onCollisionVFXPath = getString(*emitterNode, "eventOnCollisionVFX", "");
        config.events.onLifetimeThresholdEnabled = getBool(*emitterNode, "eventOnLifetimeThresholdEnabled", false);
        config.events.onLifetimeThresholdVFXPath = getString(*emitterNode, "eventOnLifetimeThresholdVFX", "");
        config.events.lifetimeThreshold = std::clamp(
            getFloat(*emitterNode, "eventLifetimeThreshold", EventDefaults::LIFETIME_THRESHOLD), 0.0f, 1.0f);

        // Lighting
        config.lightingInfluence = std::clamp(
            getFloat(*emitterNode, "lightingInfluence", EmitterDefaults::LIGHTING_INFLUENCE), 0.0f, 1.0f);
        config.normalMode = std::clamp(
            getInt(*emitterNode, "normalMode", EmitterDefaults::NORMAL_MODE), 0, 2);
        config.ambientAmount = std::clamp(
            getFloat(*emitterNode, "ambientAmount", EmitterDefaults::AMBIENT_AMOUNT), 0.0f, 1.0f);

        // Proxy light emission
        config.lightEmissionEnabled = getBool(*emitterNode, "lightEmissionEnabled", EmitterDefaults::LIGHT_EMISSION_ENABLED);
        config.lightEmissionIntensity = std::max(0.0f,
            getFloat(*emitterNode, "lightEmissionIntensity", EmitterDefaults::LIGHT_EMISSION_INTENSITY));
        config.lightEmissionRadius = std::max(0.1f,
            getFloat(*emitterNode, "lightEmissionRadius", EmitterDefaults::LIGHT_EMISSION_RADIUS));

        // Collision
        config.collisionEnabled = getBool(*emitterNode, "collisionEnabled", EmitterDefaults::COLLISION_ENABLED);
        config.collisionBounce = std::clamp(
            getFloat(*emitterNode, "collisionBounce", EmitterDefaults::COLLISION_BOUNCE), 0.0f, 1.0f);
        config.collisionFriction = std::clamp(
            getFloat(*emitterNode, "collisionFriction", EmitterDefaults::COLLISION_FRICTION), 0.0f, 1.0f);
        config.collisionLifetimeLoss = std::clamp(
            getFloat(*emitterNode, "collisionLifetimeLoss", EmitterDefaults::COLLISION_LIFETIME_LOSS), 0.0f, 1.0f);

        // Distortion
        config.distortionEnabled = getBool(*emitterNode, "distortionEnabled", false);
        config.distortionStrength = std::clamp(
            getFloat(*emitterNode, "distortionStrength", 0.1f), 0.0f, 2.0f);
        config.distortionTexturePath = getString(*emitterNode, "distortionTexture", "");

        // VK-1453 (Phase 4) — carry the per-asset scalability profile (CPU-only,
        // disabled by default so it resolves to a neutral level).
        config.scalability = data.scalability;

        return config;
    }
}
