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
        case VFXPropertyType::Float:  return "Float";
        case VFXPropertyType::Vec2:   return "Vec2";
        case VFXPropertyType::Vec3:   return "Vec3";
        case VFXPropertyType::Vec4:   return "Vec4";
        case VFXPropertyType::Color:  return "Color";
        case VFXPropertyType::Int:    return "Int";
        case VFXPropertyType::Bool:   return "Bool";
        case VFXPropertyType::String: return "String";
        default: return "Float";
        }
    }

    VFXPropertyType stringToPropertyType(const std::string& str)
    {
        if (str == "Float")  return VFXPropertyType::Float;
        if (str == "Vec2")   return VFXPropertyType::Vec2;
        if (str == "Vec3")   return VFXPropertyType::Vec3;
        if (str == "Vec4")   return VFXPropertyType::Vec4;
        if (str == "Color")  return VFXPropertyType::Color;
        if (str == "Int")    return VFXPropertyType::Int;
        if (str == "Bool")   return VFXPropertyType::Bool;
        if (str == "String") return VFXPropertyType::String;
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

    bool VFXGraph::isValid() const
    {
        // Check if Emitter node exists
        const VFXNode* emitter = findEmitterNode();
        if (!emitter)
            return false;

        // Check if OutSystem node exists
        const VFXNode* outSystem = findOutSystemNode();
        if (!outSystem)
            return false;

        // Check if there's a link from Emitter to OutSystem
        for (const auto& link : links)
        {
            if (link.sourceNodeId == emitter->id && link.targetNodeId == outSystem->id)
                return true;
        }

        return false;
    }

    std::string VFXGraph::getValidationError() const
    {
        const VFXNode* emitter = findEmitterNode();
        if (!emitter)
            return "Missing Emitter node";

        const VFXNode* outSystem = findOutSystemNode();
        if (!outSystem)
            return "Missing OutSystem node";

        // Check if there's a link from Emitter to OutSystem
        for (const auto& link : links)
        {
            if (link.sourceNodeId == emitter->id && link.targetNodeId == outSystem->id)
                return "";  // No error
        }

        return "Emitter is not connected to OutSystem";
    }
}
