#pragma once

#include <value/ValueType.hpp>
#include <value/NativeArray.hpp>
#include <runtimeTypes/klass/ObjectInstance.hpp>
#include "../../../services/data/EntityHandle.hpp"
#include "print/EditorLogger.hpp"

#include <string>
#include <optional>

namespace core::api
{
    inline std::string extractString(const value::Value& val, const char* context = nullptr)
    {
        if (std::holds_alternative<std::string>(val))
        {
            return std::get<std::string>(val);
        }
        if (std::holds_alternative<value::InternedString>(val))
        {
            return std::get<value::InternedString>(val).getString();
        }
        // Check for boxed String object
        if (std::holds_alternative<std::shared_ptr<runtimeTypes::klass::ObjectInstance>>(val))
        {
            auto obj = std::get<std::shared_ptr<runtimeTypes::klass::ObjectInstance>>(val);
            if (obj)
            {
                if (obj->getTypeName() == "String")
                {
                    auto fieldVal = obj->getFieldValue("value");
                    if (std::holds_alternative<std::string>(fieldVal))
                    {
                        return std::get<std::string>(fieldVal);
                    }
                    if (std::holds_alternative<value::InternedString>(fieldVal))
                    {
                        return std::get<value::InternedString>(fieldVal).getString();
                    }
                }
                // Try to get _value field for other wrapper types
                auto valueField = obj->getFieldValue("_value");
                if (std::holds_alternative<std::string>(valueField))
                {
                    return std::get<std::string>(valueField);
                }
                if (std::holds_alternative<value::InternedString>(valueField))
                {
                    return std::get<value::InternedString>(valueField).getString();
                }
            }
        }
        if (context && !std::holds_alternative<std::monostate>(val))
        {
            vfLogError("[Script] {}: expected string argument, got variant index {}", context, val.index());
        }
        return "";
    }

    inline int64_t extractInt64(const value::Value& val, const char* context = nullptr)
    {
        if (std::holds_alternative<int64_t>(val))
        {
            return std::get<int64_t>(val);
        }
        if (context && !std::holds_alternative<std::monostate>(val))
        {
            vfLogError("[Script] {}: expected integer argument", context);
        }
        return -1;
    }

    inline float extractFloat(const value::Value& val, const char* context = nullptr)
    {
        if (std::holds_alternative<float>(val))
        {
            return std::get<float>(val);
        }
        if (std::holds_alternative<int64_t>(val))
        {
            return static_cast<float>(std::get<int64_t>(val));
        }
        if (context && !std::holds_alternative<std::monostate>(val))
        {
            vfLogError("[Script] {}: expected number argument", context);
        }
        return 0.0f;
    }

    inline int64_t entityToInt(const services::EntityHandle& handle)
    {
        if (!handle.isValid())
        {
            return -1;
        }
        return static_cast<int64_t>(handle.id);
    }

    inline services::EntityHandle intToEntity(int64_t id)
    {
        if (id < 0)
        {
            return services::EntityHandle::invalid();
        }
        return services::EntityHandle{static_cast<uint64_t>(id)};
    }

    inline std::optional<services::ComponentTypeId> stringToComponentType(const std::string& type)
    {
        if (type == "Transform") return services::ComponentTypeId::Transform;
        if (type == "Camera") return services::ComponentTypeId::Camera;
        if (type == "Name") return services::ComponentTypeId::Name;
        if (type == "Parent") return services::ComponentTypeId::Parent;
        if (type == "Children") return services::ComponentTypeId::Children;
        if (type == "WorldTransform") return services::ComponentTypeId::WorldTransform;
        if (type == "IBL") return services::ComponentTypeId::IBL;
        if (type == "Mesh") return services::ComponentTypeId::Mesh;
        if (type == "DirectionalLight") return services::ComponentTypeId::DirectionalLight;
        if (type == "PointLight") return services::ComponentTypeId::PointLight;
        if (type == "SpotLight") return services::ComponentTypeId::SpotLight;
        if (type == "Material") return services::ComponentTypeId::Material;
        if (type == "Billboard") return services::ComponentTypeId::Billboard;
        if (type == "AudioSource2D") return services::ComponentTypeId::AudioSource2D;
        if (type == "AudioSource3D") return services::ComponentTypeId::AudioSource3D;
        if (type == "Script") return services::ComponentTypeId::Script;
        if (type == "Collider") return services::ComponentTypeId::Collider;
        if (type == "RigidBody") return services::ComponentTypeId::RigidBody;
        if (type == "Animator") return services::ComponentTypeId::Animator;
        if (type == "VFX") return services::ComponentTypeId::VFX;
        if (type == "Text") return services::ComponentTypeId::Text;
        if (type == "UICanvas") return services::ComponentTypeId::UICanvas;
        if (type == "UIRect") return services::ComponentTypeId::UIRect;
        if (type == "UIImage") return services::ComponentTypeId::UIImage;
        if (type == "UIScroll") return services::ComponentTypeId::UIScroll;
        if (type == "UILayoutGroup") return services::ComponentTypeId::UILayoutGroup;
        if (type == "UILabel") return services::ComponentTypeId::UILabel;
        if (type == "UIButton") return services::ComponentTypeId::UIButton;
        if (type == "UITextInput") return services::ComponentTypeId::UITextInput;
        if (type == "UICheckbox") return services::ComponentTypeId::UICheckbox;
        if (type == "UIDropdown") return services::ComponentTypeId::UIDropdown;
        if (type == "UITabs") return services::ComponentTypeId::UITabs;
        if (type == "UISlider") return services::ComponentTypeId::UISlider;
        if (type == "UIProgressBar") return services::ComponentTypeId::UIProgressBar;
        vfLogError("[Script] Unknown component type: '{}'", type);
        return std::nullopt;
    }

    inline std::string componentTypeToString(services::ComponentTypeId type)
    {
        switch (type)
        {
        case services::ComponentTypeId::Transform: return "Transform";
        case services::ComponentTypeId::Camera: return "Camera";
        case services::ComponentTypeId::Name: return "Name";
        case services::ComponentTypeId::Parent: return "Parent";
        case services::ComponentTypeId::Children: return "Children";
        case services::ComponentTypeId::WorldTransform: return "WorldTransform";
        case services::ComponentTypeId::IBL: return "IBL";
        case services::ComponentTypeId::Mesh: return "Mesh";
        case services::ComponentTypeId::DirectionalLight: return "DirectionalLight";
        case services::ComponentTypeId::PointLight: return "PointLight";
        case services::ComponentTypeId::SpotLight: return "SpotLight";
        case services::ComponentTypeId::Material: return "Material";
        case services::ComponentTypeId::Billboard: return "Billboard";
        case services::ComponentTypeId::AudioSource2D: return "AudioSource2D";
        case services::ComponentTypeId::AudioSource3D: return "AudioSource3D";
        case services::ComponentTypeId::Script: return "Script";
        case services::ComponentTypeId::Collider: return "Collider";
        case services::ComponentTypeId::RigidBody: return "RigidBody";
        case services::ComponentTypeId::Animator: return "Animator";
        case services::ComponentTypeId::VFX: return "VFX";
        case services::ComponentTypeId::Text: return "Text";
        case services::ComponentTypeId::UICanvas: return "UICanvas";
        case services::ComponentTypeId::UIRect: return "UIRect";
        case services::ComponentTypeId::UIImage: return "UIImage";
        case services::ComponentTypeId::UIScroll: return "UIScroll";
        case services::ComponentTypeId::UILayoutGroup: return "UILayoutGroup";
        case services::ComponentTypeId::UILabel: return "UILabel";
        case services::ComponentTypeId::UIButton: return "UIButton";
        case services::ComponentTypeId::UITextInput: return "UITextInput";
        case services::ComponentTypeId::UICheckbox: return "UICheckbox";
        case services::ComponentTypeId::UIDropdown: return "UIDropdown";
        case services::ComponentTypeId::UITabs: return "UITabs";
        case services::ComponentTypeId::UISlider: return "UISlider";
        case services::ComponentTypeId::UIProgressBar: return "UIProgressBar";
        default: return "Unknown";
        }
    }
}
