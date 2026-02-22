#include "VFXEmitterConfigLoader.hpp"
#include <algorithm>

namespace vfx
{
    float VFXEmitterConfigLoader::getFloat(const VFXNode& node, const std::string& propName, float defaultValue)
    {
        return getPropertyValue<float>(node, propName, defaultValue);
    }

    int32_t VFXEmitterConfigLoader::getInt(const VFXNode& node, const std::string& propName, int32_t defaultValue)
    {
        return getPropertyValue<int32_t>(node, propName, defaultValue);
    }

    glm::vec3 VFXEmitterConfigLoader::getVec3(const VFXNode& node, const std::string& propName, const glm::vec3& defaultValue)
    {
        return getPropertyValue<glm::vec3>(node, propName, defaultValue);
    }

    glm::vec4 VFXEmitterConfigLoader::getVec4(const VFXNode& node, const std::string& propName, const glm::vec4& defaultValue)
    {
        return getPropertyValue<glm::vec4>(node, propName, defaultValue);
    }

    bool VFXEmitterConfigLoader::getBool(const VFXNode& node, const std::string& propName, bool defaultValue)
    {
        return getPropertyValue<bool>(node, propName, defaultValue);
    }

    std::string VFXEmitterConfigLoader::getString(const VFXNode& node, const std::string& propName, const std::string& defaultValue)
    {
        return getPropertyValue<std::string>(node, propName, defaultValue);
    }

    std::optional<render::vfx::VFXEmitterConfig> VFXEmitterConfigLoader::loadFromFile(std::string_view path)
    {
        auto vfxData = VFXAsset::load(path);
        if (!vfxData.has_value())
        {
            return std::nullopt;
        }

        return fromVFXData(vfxData.value());
    }

    render::vfx::VFXEmitterConfig VFXEmitterConfigLoader::fromVFXData(const VFXData& data)
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

        config.modifiers = VFXModifierConfigLoader::fromGraph(data.graph);
        config.forces = VFXForceConfigLoader::fromGraph(data.graph);
        config.shape = VFXShapeConfigLoader::fromGraph(data.graph);

        config.flipbookRows = std::clamp(getInt(*emitterNode, "flipbookRows", EmitterDefaults::FLIPBOOK_ROWS), 1, 16);
        config.flipbookColumns = std::clamp(getInt(*emitterNode, "flipbookColumns", EmitterDefaults::FLIPBOOK_COLUMNS), 1, 16);
        config.flipbookFrameRate = getFloat(*emitterNode, "flipbookFrameRate", EmitterDefaults::FLIPBOOK_FRAME_RATE);
        config.flipbookRandomStart = getBool(*emitterNode, "flipbookRandomStart", EmitterDefaults::FLIPBOOK_RANDOM_START);

        config.alphaClipThreshold = getFloat(*emitterNode, "alphaClipThreshold", EmitterDefaults::ALPHA_CLIP_THRESHOLD);
        config.additiveBlend = getBool(*emitterNode, "additiveBlend", EmitterDefaults::ADDITIVE_BLEND);

        return config;
    }
}
