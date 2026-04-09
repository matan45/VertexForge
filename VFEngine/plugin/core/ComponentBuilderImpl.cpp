#include "ComponentBuilderImpl.hpp"
#include "PluginContextImpl.hpp"
#include "print/Log.hpp"

namespace plugin
{
    std::vector<components::plugin::PropertyDescriptor>& ComponentBuilderImpl::currentProperties()
    {
        if (nestedStack.empty())
            return properties;
        return *nestedStack.back();
    }

    ComponentBuilder& ComponentBuilderImpl::addInt(const std::string& name, int defaultVal, float min, float max)
    {
        currentProperties().push_back({name, components::plugin::PropertyType::Int,
                              nlohmann::json(defaultVal), min, max, {}});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addFloat(const std::string& name, float defaultVal, float min, float max)
    {
        currentProperties().push_back({name, components::plugin::PropertyType::Float,
                              nlohmann::json(defaultVal), min, max, {}});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addBool(const std::string& name, bool defaultVal)
    {
        currentProperties().push_back({name, components::plugin::PropertyType::Bool,
                              nlohmann::json(defaultVal), 0, 0, {}});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addString(const std::string& name, const std::string& defaultVal)
    {
        currentProperties().push_back({name, components::plugin::PropertyType::String,
                              nlohmann::json(defaultVal), 0, 0, {}});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addVec2(const std::string& name, glm::vec2 defaultVal)
    {
        currentProperties().push_back({name, components::plugin::PropertyType::Vec2,
                              nlohmann::json::array({defaultVal.x, defaultVal.y}), 0, 0, {}});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addVec3(const std::string& name, glm::vec3 defaultVal)
    {
        currentProperties().push_back({name, components::plugin::PropertyType::Vec3,
                              nlohmann::json::array({defaultVal.x, defaultVal.y, defaultVal.z}), 0, 0, {}});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addVec4(const std::string& name, glm::vec4 defaultVal)
    {
        currentProperties().push_back({name, components::plugin::PropertyType::Vec4,
                              nlohmann::json::array({defaultVal.x, defaultVal.y, defaultVal.z, defaultVal.w}), 0, 0, {}});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addColor(const std::string& name, glm::vec4 defaultVal)
    {
        currentProperties().push_back({name, components::plugin::PropertyType::Color,
                              nlohmann::json::array({defaultVal.x, defaultVal.y, defaultVal.z, defaultVal.w}), 0, 0, {}});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addEnum(const std::string& name,
                                                     const std::vector<std::string>& options, int defaultIndex)
    {
        components::plugin::PropertyDescriptor desc;
        desc.name = name;
        desc.type = components::plugin::PropertyType::Enum;
        desc.defaultValue = nlohmann::json(defaultIndex);
        desc.enumOptions = options;
        currentProperties().push_back(std::move(desc));
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addAssetRef(const std::string& name, const std::string& fileFilter)
    {
        components::plugin::PropertyDescriptor desc;
        desc.name = name;
        desc.type = components::plugin::PropertyType::AssetRef;
        desc.defaultValue = nlohmann::json("");
        desc.assetTypeFilter = fileFilter;
        currentProperties().push_back(std::move(desc));
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addEntityRef(const std::string& name)
    {
        components::plugin::PropertyDescriptor desc;
        desc.name = name;
        desc.type = components::plugin::PropertyType::EntityRef;
        desc.defaultValue = nlohmann::json(static_cast<uint64_t>(0));
        currentProperties().push_back(std::move(desc));
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addQuat(const std::string& name, glm::quat defaultVal)
    {
        currentProperties().push_back({name, components::plugin::PropertyType::Quaternion,
                              nlohmann::json::array({defaultVal.x, defaultVal.y, defaultVal.z, defaultVal.w}), 0, 0, {}});
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addArray(const std::string& name)
    {
        currentProperties().push_back({name, components::plugin::PropertyType::Array,
                              nlohmann::json::array(), 0, 0, {}});
        // Push the children of the newly added array descriptor onto the stack
        nestedStack.push_back(&currentProperties().back().children);
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::endArray()
    {
        if (!nestedStack.empty())
            nestedStack.pop_back();
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::addObject(const std::string& name)
    {
        // Build default as empty object
        currentProperties().push_back({name, components::plugin::PropertyType::Object,
                              nlohmann::json::object(), 0, 0, {}});
        nestedStack.push_back(&currentProperties().back().children);
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::endObject()
    {
        if (!nestedStack.empty())
        {
            // Build the default value from children descriptors
            auto* children = nestedStack.back();
            nestedStack.pop_back();

            // Find the object descriptor we just finished defining
            auto& parent = currentProperties().back();
            if (parent.type == components::plugin::PropertyType::Object)
            {
                nlohmann::json obj = nlohmann::json::object();
                for (const auto& child : parent.children)
                {
                    obj[child.name] = child.defaultValue;
                }
                parent.defaultValue = std::move(obj);
            }
        }
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::setInspector(std::function<bool(nlohmann::json&)> inspectorCallback)
    {
        inspector = std::move(inspectorCallback);
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::setOnAdded(std::function<void(entt::entity, const nlohmann::json&)> callback)
    {
        onAddedCb = std::move(callback);
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::setOnRemoved(std::function<void(entt::entity)> callback)
    {
        onRemovedCb = std::move(callback);
        return *this;
    }

    ComponentBuilder& ComponentBuilderImpl::setOnDataChanged(std::function<void(entt::entity, const nlohmann::json&)> callback)
    {
        onDataChangedCb = std::move(callback);
        return *this;
    }

    void ComponentBuilderImpl::build()
    {
        if (!nestedStack.empty())
        {
            vfLogWarning("Plugin component '{}' has {} unclosed addArray/addObject call(s) — auto-closing",
                         componentName, nestedStack.size());
            nestedStack.clear();
        }
        context->finalizeComponentRegistration(*this);
    }
}
