#include "ComponentBuilderImpl.hpp"
#include "PluginContextImpl.hpp"

namespace plugin
{
    ComponentBuilder& ComponentBuilderImpl::addInt(const std::string& name, int defaultVal, float min, float max)
    {
        properties.push_back({name, components::plugin::PropertyType::Int,
                              nlohmann::json(defaultVal), min, max});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addFloat(const std::string& name, float defaultVal, float min, float max)
    {
        properties.push_back({name, components::plugin::PropertyType::Float,
                              nlohmann::json(defaultVal), min, max});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addBool(const std::string& name, bool defaultVal)
    {
        properties.push_back({name, components::plugin::PropertyType::Bool,
                              nlohmann::json(defaultVal), 0, 0});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addString(const std::string& name, const std::string& defaultVal)
    {
        properties.push_back({name, components::plugin::PropertyType::String,
                              nlohmann::json(defaultVal), 0, 0});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addVec2(const std::string& name, glm::vec2 defaultVal)
    {
        properties.push_back({name, components::plugin::PropertyType::Vec2,
                              nlohmann::json::array({defaultVal.x, defaultVal.y}), 0, 0});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addVec3(const std::string& name, glm::vec3 defaultVal)
    {
        properties.push_back({name, components::plugin::PropertyType::Vec3,
                              nlohmann::json::array({defaultVal.x, defaultVal.y, defaultVal.z}), 0, 0});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addVec4(const std::string& name, glm::vec4 defaultVal)
    {
        properties.push_back({name, components::plugin::PropertyType::Vec4,
                              nlohmann::json::array({defaultVal.x, defaultVal.y, defaultVal.z, defaultVal.w}), 0, 0});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addColor(const std::string& name, glm::vec4 defaultVal)
    {
        properties.push_back({name, components::plugin::PropertyType::Color,
                              nlohmann::json::array({defaultVal.x, defaultVal.y, defaultVal.z, defaultVal.w}), 0, 0});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::setInspector(std::function<bool(nlohmann::json&)> inspectorCallback)
    {
        inspector = std::move(inspectorCallback);
        return *this;
    }

    void ComponentBuilderImpl::build()
    {
        context->finalizeComponentRegistration(*this);
    }
}
