#pragma once

#include "VFXTypes.hpp"
#include "VFXAsset.hpp"
#include "VFXModifierConfigLoader.hpp"
#include "VFXForceConfigLoader.hpp"
#include "VFXShapeConfigLoader.hpp"
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
}
