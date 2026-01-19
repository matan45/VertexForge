#include "VFXTypes.hpp"

namespace vfx
{
    VFXNode* VFXGraph::findNode(uint32_t nodeId)
    {
        for (auto& node : nodes)
        {
            if (node.id == nodeId)
                return &node;
        }
        return nullptr;
    }

    const VFXNode* VFXGraph::findNode(uint32_t nodeId) const
    {
        for (const auto& node : nodes)
        {
            if (node.id == nodeId)
                return &node;
        }
        return nullptr;
    }

    const VFXNode* VFXGraph::findEmitterNode() const
    {
        for (const auto& node : nodes)
        {
            if (node.type == VFXNodeType::Emitter)
                return &node;
        }
        return nullptr;
    }

    const VFXNode* VFXGraph::findOutSystemNode() const
    {
        for (const auto& node : nodes)
        {
            if (node.type == VFXNodeType::OutSystem)
                return &node;
        }
        return nullptr;
    }

    const char* propertyTypeToString(VFXPropertyType type)
    {
        switch (type)
        {
        case VFXPropertyType::Float: return "Float";
        case VFXPropertyType::Vec2:  return "Vec2";
        case VFXPropertyType::Vec3:  return "Vec3";
        case VFXPropertyType::Vec4:  return "Vec4";
        case VFXPropertyType::Color: return "Color";
        case VFXPropertyType::Int:   return "Int";
        case VFXPropertyType::Bool:  return "Bool";
        default: return "Float";
        }
    }

    VFXPropertyType stringToPropertyType(const std::string& str)
    {
        if (str == "Float") return VFXPropertyType::Float;
        if (str == "Vec2")  return VFXPropertyType::Vec2;
        if (str == "Vec3")  return VFXPropertyType::Vec3;
        if (str == "Vec4")  return VFXPropertyType::Vec4;
        if (str == "Color") return VFXPropertyType::Color;
        if (str == "Int")   return VFXPropertyType::Int;
        if (str == "Bool")  return VFXPropertyType::Bool;
        return VFXPropertyType::Float;
    }

    const char* nodeTypeToString(VFXNodeType type)
    {
        switch (type)
        {
        case VFXNodeType::Emitter:   return "Emitter";
        case VFXNodeType::OutSystem: return "OutSystem";
        default: return "Emitter";
        }
    }

    VFXNodeType stringToNodeType(const std::string& str)
    {
        if (str == "Emitter")   return VFXNodeType::Emitter;
        if (str == "OutSystem") return VFXNodeType::OutSystem;
        return VFXNodeType::Emitter;
    }
}
