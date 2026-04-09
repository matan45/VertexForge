#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
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
        Color,
        Array,      // dynamic-length list, element schema defined by children[0]
        Object,     // fixed struct, fields defined by children
        Enum,       // dropdown selection, options in enumOptions, value stored as int index
        AssetRef,   // asset file path, filtered by assetTypeFilter
        EntityRef,  // entity reference, stored as uint64_t entity handle ID
        Quaternion  // rotation quaternion, displayed as euler angles
    };

    struct PropertyDescriptor
    {
        std::string name;
        PropertyType type;
        nlohmann::json defaultValue;
        float min = 0.0f;
        float max = 0.0f;
        std::vector<PropertyDescriptor> children; // element schema (Array) or fields (Object)
        std::vector<std::string> enumOptions;     // for Enum: selectable option labels
        std::string assetTypeFilter;              // for AssetRef: file filter (e.g. "Mesh|*.vfmesh")
    };
}
