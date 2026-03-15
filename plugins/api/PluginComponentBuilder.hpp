#pragma once
#include "components/PluginPropertyTypes.hpp"
#include <nlohmann/json.hpp>
#include <glm/glm.hpp>
#include <functional>
#include <string>
#include <vector>

namespace plugin
{
    class PluginContextImpl;

    class ComponentBuilder
    {
    public:
        ComponentBuilder(PluginContextImpl* context, const std::string& componentName)
            : context(context), componentName(componentName)
        {
        }

        ComponentBuilder& addInt(const std::string& name, int defaultVal, float min = 0, float max = 0)
        {
            properties.push_back({name, components::plugin::PropertyType::Int,
                                  nlohmann::json(defaultVal), min, max});
            return *this;
        }

        ComponentBuilder& addFloat(const std::string& name, float defaultVal, float min = 0, float max = 0)
        {
            properties.push_back({name, components::plugin::PropertyType::Float,
                                  nlohmann::json(defaultVal), min, max});
            return *this;
        }

        ComponentBuilder& addBool(const std::string& name, bool defaultVal)
        {
            properties.push_back({name, components::plugin::PropertyType::Bool,
                                  nlohmann::json(defaultVal), 0, 0});
            return *this;
        }

        ComponentBuilder& addString(const std::string& name, const std::string& defaultVal)
        {
            properties.push_back({name, components::plugin::PropertyType::String,
                                  nlohmann::json(defaultVal), 0, 0});
            return *this;
        }

        ComponentBuilder& addVec2(const std::string& name, glm::vec2 defaultVal)
        {
            properties.push_back({name, components::plugin::PropertyType::Vec2,
                                  nlohmann::json::array({defaultVal.x, defaultVal.y}), 0, 0});
            return *this;
        }

        ComponentBuilder& addVec3(const std::string& name, glm::vec3 defaultVal)
        {
            properties.push_back({name, components::plugin::PropertyType::Vec3,
                                  nlohmann::json::array({defaultVal.x, defaultVal.y, defaultVal.z}), 0, 0});
            return *this;
        }

        ComponentBuilder& addVec4(const std::string& name, glm::vec4 defaultVal)
        {
            properties.push_back({name, components::plugin::PropertyType::Vec4,
                                  nlohmann::json::array({defaultVal.x, defaultVal.y, defaultVal.z, defaultVal.w}), 0, 0});
            return *this;
        }

        ComponentBuilder& addColor(const std::string& name, glm::vec4 defaultVal)
        {
            properties.push_back({name, components::plugin::PropertyType::Color,
                                  nlohmann::json::array({defaultVal.x, defaultVal.y, defaultVal.z, defaultVal.w}), 0, 0});
            return *this;
        }

        ComponentBuilder& setInspector(std::function<bool(nlohmann::json&)> inspectorCallback)
        {
            inspector = std::move(inspectorCallback);
            return *this;
        }

        // Finalizes registration. Implemented in PluginContextImpl.cpp.
        void build();

        const std::string& getComponentName() const { return componentName; }
        const std::vector<components::plugin::PropertyDescriptor>& getProperties() const { return properties; }
        const std::function<bool(nlohmann::json&)>& getInspector() const { return inspector; }

    private:
        PluginContextImpl* context;
        std::string componentName;
        std::vector<components::plugin::PropertyDescriptor> properties;
        std::function<bool(nlohmann::json&)> inspector;
    };
}
