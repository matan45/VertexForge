#pragma once
#include "../api/PluginComponentBuilder.hpp"
#include <string>
#include <vector>
#include <functional>

namespace plugin
{
    class PluginContextImpl;

    class ComponentBuilderImpl : public ComponentBuilder
    {
    public:
        ComponentBuilderImpl(PluginContextImpl* context, const std::string& componentName)
            : context(context), componentName(componentName)
        {
        }

        ComponentBuilder& addInt(const std::string& name, int defaultVal, float min, float max) override;
        ComponentBuilder& addFloat(const std::string& name, float defaultVal, float min, float max) override;
        ComponentBuilder& addBool(const std::string& name, bool defaultVal) override;
        ComponentBuilder& addString(const std::string& name, const std::string& defaultVal) override;
        ComponentBuilder& addVec2(const std::string& name, glm::vec2 defaultVal) override;
        ComponentBuilder& addVec3(const std::string& name, glm::vec3 defaultVal) override;
        ComponentBuilder& addVec4(const std::string& name, glm::vec4 defaultVal) override;
        ComponentBuilder& addColor(const std::string& name, glm::vec4 defaultVal) override;
        ComponentBuilder& setInspector(std::function<bool(nlohmann::json&)> inspectorCallback) override;
        void build() override;

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
