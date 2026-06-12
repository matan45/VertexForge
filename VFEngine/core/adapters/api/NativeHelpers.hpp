#pragma once

#include <value/ValueType.hpp>
#include <value/ValueShim.hpp>
#include <value/NativeArray.hpp>
#include <value/ObjectInstance.hpp>
#include "../../../services/data/EntityHandle.hpp"
#include "../../../services/data/EntityConversion.hpp"
#include "scene/EntityRegistry.hpp"
#include "print/Log.hpp"

#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include "weather/WeatherTypes.hpp"
#include <string>
#include <optional>

namespace core::api
{
    inline std::string extractString(const value::Value& val, const char* context = nullptr)
    {
        if (value::isString(val))
        {
            return value::asString(val);
        }
        if (value::isInternedString(val))
        {
            return value::asInternedString(val).getString();
        }
        // Check for boxed String object
        if (value::isObject(val))
        {
            const auto& obj = value::asObject(val);
            if (obj)
            {
                if (obj->getTypeName() == "String")
                {
                    auto fieldVal = obj->getFieldValue("value");
                    if (value::isString(fieldVal))
                    {
                        return value::asString(fieldVal);
                    }
                    if (value::isInternedString(fieldVal))
                    {
                        return value::asInternedString(fieldVal).getString();
                    }
                }
                // Try to get _value field for other wrapper types
                auto valueField = obj->getFieldValue("_value");
                if (value::isString(valueField))
                {
                    return value::asString(valueField);
                }
                if (value::isInternedString(valueField))
                {
                    return value::asInternedString(valueField).getString();
                }
            }
        }
        if (context && !value::isVoid(val))
        {
            vfLogError("[Script] {}: expected string argument, got tag {}",
                       context, static_cast<int>(val.tag()));
        }
        return "";
    }

    inline int64_t extractInt64(const value::Value& val, const char* context = nullptr)
    {
        if (value::isInt(val))
        {
            return value::asInt(val);
        }
        if (context && !value::isVoid(val))
        {
            vfLogError("[Script] {}: expected integer argument", context);
        }
        return -1;
    }

    inline float extractFloat(const value::Value& val, const char* context = nullptr)
    {
        if (value::isFloat(val))
        {
            return static_cast<float>(value::asFloat(val));
        }
        if (value::isInt(val))
        {
            return static_cast<float>(value::asInt(val));
        }
        if (context && !value::isVoid(val))
        {
            vfLogError("[Script] {}: expected number argument", context);
        }
        return 0.0f;
    }

    inline bool extractBool(const value::Value& val, const char* context = nullptr)
    {
        if (value::isBool(val))
        {
            return value::asBool(val);
        }
        if (context && !value::isVoid(val))
        {
            vfLogError("[Script] {}: expected boolean argument", context);
        }
        return false;
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
        if (type == "PhysicsAnimation") return services::ComponentTypeId::PhysicsAnimation;
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
        if (type == "UIStyle") return services::ComponentTypeId::UIStyle;
        if (type == "SocketAttachment") return services::ComponentTypeId::SocketAttachment;
        if (type == "SocketOverride") return services::ComponentTypeId::SocketOverride;
        if (type == "NavmeshAgent") return services::ComponentTypeId::NavmeshAgent;
        if (type == "Controller") return services::ComponentTypeId::Controller;
        if (type == "Decal") return services::ComponentTypeId::Decal;
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
        case services::ComponentTypeId::PhysicsAnimation: return "PhysicsAnimation";
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
        case services::ComponentTypeId::UIStyle: return "UIStyle";
        case services::ComponentTypeId::SocketAttachment: return "SocketAttachment";
        case services::ComponentTypeId::SocketOverride: return "SocketOverride";
        case services::ComponentTypeId::NavmeshAgent: return "NavmeshAgent";
        case services::ComponentTypeId::Controller: return "Controller";
        case services::ComponentTypeId::Decal: return "Decal";
        default: return "Unknown";
        }
    }

    inline std::optional<entt::entity> resolveEntity(const value::Value& val)
    {
        int64_t id = extractInt64(val);
        if (id < 0)
        {
            return std::nullopt;
        }
        auto& registry = scene::EntityRegistry::getRegistry();
        auto entity = services::internal::fromHandle(
            services::EntityHandle{static_cast<uint64_t>(id)});
        if (!registry.valid(entity))
        {
            return std::nullopt;
        }
        return entity;
    }

    inline value::Value makeVec3Array(const glm::vec3& v)
    {
        auto arr = std::make_shared<value::NativeArray>(3, value::ValueType::FLOAT);
        arr->set(0, value::Value(v.x));
        arr->set(1, value::Value(v.y));
        arr->set(2, value::Value(v.z));
        return value::Value(arr);
    }

    // cloud(3) + precip(2) + wind(4) + fog(2) + ambientLightMult(1) + atmosphereTint(3) = 15
    inline constexpr int WEATHER_STATE_ARRAY_SIZE = 15;

    inline value::Value makeWeatherStateArray(const weather::WeatherState& s)
    {
        auto arr = std::make_shared<value::NativeArray>(WEATHER_STATE_ARRAY_SIZE, value::ValueType::FLOAT);
        arr->set(0, value::Value(s.cloudCoverage));
        arr->set(1, value::Value(s.cloudDensity));
        arr->set(2, value::Value(s.cloudType));
        arr->set(3, value::Value(static_cast<double>(static_cast<uint8_t>(s.precipType))));
        arr->set(4, value::Value(s.precipIntensity));
        arr->set(5, value::Value(s.windSpeed));
        arr->set(6, value::Value(s.windDirectionDeg));
        arr->set(7, value::Value(s.gustStrength));
        arr->set(8, value::Value(s.gustFrequency));
        arr->set(9, value::Value(s.fogDensity));
        arr->set(10, value::Value(s.heightFogDensity));
        arr->set(11, value::Value(s.ambientLightMult));
        arr->set(12, value::Value(s.atmosphereTint.x));
        arr->set(13, value::Value(s.atmosphereTint.y));
        arr->set(14, value::Value(s.atmosphereTint.z));
        return value::Value(arr);
    }

    // Returns 16 floats in row-major order to match mType's Matrix4f layout.
    // GLM stores column-major: m[col][row], so we transpose to row-major.
    inline value::Value makeMat4Array(const glm::mat4& m)
    {
        auto arr = std::make_shared<value::NativeArray>(16, value::ValueType::FLOAT);
        for (int row = 0; row < 4; ++row)
        {
            for (int col = 0; col < 4; ++col)
            {
                arr->set(row * 4 + col, value::Value(m[col][row]));
            }
        }
        return value::Value(arr);
    }
}
