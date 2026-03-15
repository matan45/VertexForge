#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

namespace components::plugin
{
    enum class PropertyType : uint8_t
    {
        Int,
        Float,
        Bool,
        String,
        Vec2,
        Vec3,
        Vec4,
        Color
    };

    struct PropertyDescriptor
    {
        std::string name;
        PropertyType type;
        nlohmann::json defaultValue;
        float min = 0.0f;
        float max = 0.0f;
    };
}
