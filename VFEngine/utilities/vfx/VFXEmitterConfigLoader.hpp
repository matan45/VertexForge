#pragma once

#include "VFXTypes.hpp"
#include "VFXAsset.hpp"
#include "VFXModifierConfigLoader.hpp"
#include "VFXForceConfigLoader.hpp"
#include <optional>
#include <string_view>

namespace vfx
{
    class VFXEmitterConfigLoader
    {
    public:
        // Load VFXEmitterConfig from a .vfVFX file path
        static std::optional<render::vfx::VFXEmitterConfig> loadFromFile(std::string_view path);

        // Extract VFXEmitterConfig from already-loaded VFXData
        static render::vfx::VFXEmitterConfig fromVFXData(const VFXData& data);

    private:
        // Extract property value with type-safe default fallback
        template<typename T>
        static T getPropertyValue(const VFXNode& node, const std::string& propName, T defaultValue);

        // Specialized extractors for common types
        static float getFloat(const VFXNode& node, const std::string& propName, float defaultValue);
        static glm::vec3 getVec3(const VFXNode& node, const std::string& propName, const glm::vec3& defaultValue);
        static glm::vec4 getVec4(const VFXNode& node, const std::string& propName, const glm::vec4& defaultValue);
        static bool getBool(const VFXNode& node, const std::string& propName, bool defaultValue);
        static std::string getString(const VFXNode& node, const std::string& propName, const std::string& defaultValue);
    };

    // Template implementation
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

        // Find emitter node in the graph
        const VFXNode* emitterNode = data.graph.findEmitterNode();
        if (emitterNode == nullptr)
        {
            // Return default config if no emitter node found
            return config;
        }

        // Extract properties from emitter node
        config.spawnRate = getFloat(*emitterNode, "spawnRate", EmitterDefaults::SPAWN_RATE);
        config.lifetime = getFloat(*emitterNode, "lifetime", EmitterDefaults::LIFETIME);
        config.startSize = getFloat(*emitterNode, "startSize", EmitterDefaults::START_SIZE);
        config.startSpeed = getFloat(*emitterNode, "startSpeed", EmitterDefaults::START_SPEED);

        // startVelocity property maps to emitDirection (normalized direction * startSpeed gives velocity)
        config.emitDirection = getVec3(*emitterNode, "startVelocity", glm::vec3(0.0f, 1.0f, 0.0f));

        // startColor is stored as Color type (same as Vec4)
        config.startColor = getVec4(*emitterNode, "startColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

        // Looping property
        config.looping = getBool(*emitterNode, "looping", EmitterDefaults::LOOPING);

        // Texture path
        config.texturePath = getString(*emitterNode, "texture", "");

        // Load modifier chain (VK-238)
        config.modifiers = VFXModifierConfigLoader::fromGraph(data.graph);

        // Load force chain (VK-239)
        config.forces = VFXForceConfigLoader::fromGraph(data.graph);

        return config;
    }
}
