#pragma once

// Builds services::VFXPreviewParams from a parsed .vfVFX graph, so multiple
// editor windows (the VFX graph editor and the VFX sequence editor) can drive
// the same GPU single-emitter preview (IVFXPreviewProvider) without duplicating
// the property-extraction code. Header-only; editor-side only.

#include <providers/vfx/IVFXPreviewProvider.hpp>
#include <vfx/VFXTypes.hpp>
#include <vfx/VFXModifierConfigLoader.hpp>
#include <vfx/VFXForceConfigLoader.hpp>
#include <vfx/VFXShapeConfigLoader.hpp>
#include <vfx/VFXBurstTypes.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <string>
#include <variant>

namespace editor::vfxeditor
{
    inline services::VFXPreviewParams buildVFXPreviewParams(const vfx::VFXData& vfxData)
    {
        services::VFXPreviewParams params;

        const vfx::VFXNode* emitterNode = vfxData.graph.findEmitterNode();
        if (!emitterNode)
            return params;

        auto getFloat = [](const vfx::VFXNode& node, const std::string& name, float def) -> float {
            auto it = node.properties.find(name);
            if (it != node.properties.end())
                if (auto* v = std::get_if<float>(&it->second.value)) return *v;
            return def;
        };
        auto getVec3 = [](const vfx::VFXNode& node, const std::string& name, const glm::vec3& def) -> glm::vec3 {
            auto it = node.properties.find(name);
            if (it != node.properties.end())
                if (auto* v = std::get_if<glm::vec3>(&it->second.value)) return *v;
            return def;
        };
        auto getVec4 = [](const vfx::VFXNode& node, const std::string& name, const glm::vec4& def) -> glm::vec4 {
            auto it = node.properties.find(name);
            if (it != node.properties.end())
                if (auto* v = std::get_if<glm::vec4>(&it->second.value)) return *v;
            return def;
        };
        auto getBool = [](const vfx::VFXNode& node, const std::string& name, bool def) -> bool {
            auto it = node.properties.find(name);
            if (it != node.properties.end())
                if (auto* v = std::get_if<bool>(&it->second.value)) return *v;
            return def;
        };
        auto getInt = [](const vfx::VFXNode& node, const std::string& name, int32_t def) -> int32_t {
            auto it = node.properties.find(name);
            if (it != node.properties.end())
                if (auto* v = std::get_if<int32_t>(&it->second.value)) return *v;
            return def;
        };
        auto getString = [](const vfx::VFXNode& node, const std::string& name, const std::string& def) -> std::string {
            auto it = node.properties.find(name);
            if (it != node.properties.end())
                if (auto* v = std::get_if<std::string>(&it->second.value)) return *v;
            return def;
        };

        params.spawnRate = getFloat(*emitterNode, "spawnRate", vfx::EmitterDefaults::SPAWN_RATE);
        params.lifetime = getFloat(*emitterNode, "lifetime", vfx::EmitterDefaults::LIFETIME);
        params.startSize = getFloat(*emitterNode, "startSize", vfx::EmitterDefaults::START_SIZE);
        params.startSpeed = getFloat(*emitterNode, "startSpeed", vfx::EmitterDefaults::START_SPEED);
        params.emitDirection = getVec3(*emitterNode, "startVelocity", glm::vec3(0.0f, 1.0f, 0.0f));
        params.startColor = getVec4(*emitterNode, "startColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
        params.looping = getBool(*emitterNode, "looping", vfx::EmitterDefaults::LOOPING);
        params.texturePath = getString(*emitterNode, "texture", "");

        params.modifiers = vfx::VFXModifierConfigLoader::fromGraph(vfxData.graph);
        params.forces = vfx::VFXForceConfigLoader::fromGraph(vfxData.graph);
        params.shape = vfx::VFXShapeConfigLoader::fromGraph(vfxData.graph);
        params.bursts = vfx::loadBurstsFromNode(*emitterNode);

        params.flipbookRows = std::clamp(getInt(*emitterNode, "flipbookRows", vfx::EmitterDefaults::FLIPBOOK_ROWS), 1, 16);
        params.flipbookColumns = std::clamp(getInt(*emitterNode, "flipbookColumns", vfx::EmitterDefaults::FLIPBOOK_COLUMNS), 1, 16);
        params.flipbookFrameRate = getFloat(*emitterNode, "flipbookFrameRate", vfx::EmitterDefaults::FLIPBOOK_FRAME_RATE);
        params.flipbookRandomStart = getBool(*emitterNode, "flipbookRandomStart", vfx::EmitterDefaults::FLIPBOOK_RANDOM_START);

        params.alphaClipThreshold = getFloat(*emitterNode, "alphaClipThreshold", vfx::EmitterDefaults::ALPHA_CLIP_THRESHOLD);
        params.additiveBlend = getBool(*emitterNode, "additiveBlend", vfx::EmitterDefaults::ADDITIVE_BLEND);
        params.renderMode = getInt(*emitterNode, "renderMode", vfx::EmitterDefaults::RENDER_MODE);
        params.softParticleDistance = getFloat(*emitterNode, "softParticleDistance", vfx::EmitterDefaults::SOFT_PARTICLE_DISTANCE);
        params.stretchMultiplier = getFloat(*emitterNode, "stretchMultiplier", vfx::EmitterDefaults::STRETCH_MULTIPLIER);
        params.meshPath = getString(*emitterNode, "meshPath", "");
        params.maxTrailPoints = getInt(*emitterNode, "maxTrailPoints", vfx::EmitterDefaults::MAX_TRAIL_POINTS);
        params.ribbonWidth = getFloat(*emitterNode, "ribbonWidth", vfx::EmitterDefaults::RIBBON_WIDTH);
        params.ribbonMinDistance = getFloat(*emitterNode, "ribbonMinDistance", vfx::EmitterDefaults::RIBBON_MIN_DISTANCE);
        params.uvScrollSpeedU = getFloat(*emitterNode, "uvScrollSpeedU", vfx::EmitterDefaults::UV_SCROLL_SPEED_U);
        params.uvScrollSpeedV = getFloat(*emitterNode, "uvScrollSpeedV", vfx::EmitterDefaults::UV_SCROLL_SPEED_V);

        params.lightingInfluence = std::clamp(getFloat(*emitterNode, "lightingInfluence", vfx::EmitterDefaults::LIGHTING_INFLUENCE), 0.0f, 1.0f);
        params.normalMode = std::clamp(getInt(*emitterNode, "normalMode", vfx::EmitterDefaults::NORMAL_MODE), 0, 2);
        params.ambientAmount = std::clamp(getFloat(*emitterNode, "ambientAmount", vfx::EmitterDefaults::AMBIENT_AMOUNT), 0.0f, 1.0f);

        params.collisionEnabled = getBool(*emitterNode, "collisionEnabled", vfx::EmitterDefaults::COLLISION_ENABLED);
        params.collisionBounce = std::clamp(getFloat(*emitterNode, "collisionBounce", vfx::EmitterDefaults::COLLISION_BOUNCE), 0.0f, 1.0f);
        params.collisionFriction = std::clamp(getFloat(*emitterNode, "collisionFriction", vfx::EmitterDefaults::COLLISION_FRICTION), 0.0f, 1.0f);
        params.collisionLifetimeLoss = std::clamp(getFloat(*emitterNode, "collisionLifetimeLoss", vfx::EmitterDefaults::COLLISION_LIFETIME_LOSS), 0.0f, 1.0f);

        return params;
    }
}
