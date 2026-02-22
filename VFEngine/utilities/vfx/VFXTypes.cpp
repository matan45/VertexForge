#include "VFXTypes.hpp"
#include <algorithm>

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
        case VFXPropertyType::String:   return "String";
        case VFXPropertyType::Curve:    return "Curve";
        case VFXPropertyType::Gradient: return "Gradient";
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
        if (str == "String")   return VFXPropertyType::String;
        if (str == "Curve")    return VFXPropertyType::Curve;
        if (str == "Gradient") return VFXPropertyType::Gradient;
        return VFXPropertyType::Float;
    }

    const char* nodeTypeToString(VFXNodeType type)
    {
        switch (type)
        {
        case VFXNodeType::Emitter:            return "Emitter";
        case VFXNodeType::OutSystem:          return "OutSystem";
        case VFXNodeType::ColorOverLifetime:  return "ColorOverLifetime";
        case VFXNodeType::SizeOverLifetime:   return "SizeOverLifetime";
        case VFXNodeType::SpeedOverLifetime:  return "SpeedOverLifetime";
        case VFXNodeType::RotationOverLifetime: return "RotationOverLifetime";
        case VFXNodeType::ForceGravity:       return "ForceGravity";
        case VFXNodeType::ForceWind:          return "ForceWind";
        case VFXNodeType::ForceTurbulence:    return "ForceTurbulence";
        case VFXNodeType::ForceVortex:        return "ForceVortex";
        case VFXNodeType::Shape:              return "Shape";
        default: return "Emitter";
        }
    }

    VFXNodeType stringToNodeType(const std::string& str)
    {
        if (str == "Emitter")            return VFXNodeType::Emitter;
        if (str == "OutSystem")          return VFXNodeType::OutSystem;
        if (str == "ColorOverLifetime")  return VFXNodeType::ColorOverLifetime;
        if (str == "SizeOverLifetime")   return VFXNodeType::SizeOverLifetime;
        if (str == "SpeedOverLifetime")  return VFXNodeType::SpeedOverLifetime;
        if (str == "RotationOverLifetime") return VFXNodeType::RotationOverLifetime;
        if (str == "ForceGravity")       return VFXNodeType::ForceGravity;
        if (str == "ForceWind")          return VFXNodeType::ForceWind;
        if (str == "ForceTurbulence")    return VFXNodeType::ForceTurbulence;
        if (str == "ForceVortex")        return VFXNodeType::ForceVortex;
        if (str == "Shape")              return VFXNodeType::Shape;
        return VFXNodeType::Emitter;
    }

    static bool hasPathToOutSystem(const VFXGraph& graph, uint32_t currentNodeId, uint32_t outSystemId,
                                   std::vector<uint32_t>& visited)
    {
        if (std::find(visited.begin(), visited.end(), currentNodeId) != visited.end())
            return false;
        visited.push_back(currentNodeId);

        if (currentNodeId == outSystemId)
            return true;

        for (const auto& link : graph.links)
        {
            if (link.sourceNodeId == currentNodeId)
            {
                const VFXNode* targetNode = graph.findNode(link.targetNodeId);
                if (!targetNode)
                    return false;

                if (targetNode->type == VFXNodeType::OutSystem ||
                    isModifierNode(targetNode->type) ||
                    isForceNode(targetNode->type))
                {
                    if (hasPathToOutSystem(graph, targetNode->id, outSystemId, visited))
                        return true;
                }
            }
        }

        return false;
    }

    bool VFXGraph::isValid() const
    {
        const VFXNode* emitter = findEmitterNode();
        if (!emitter)
            return false;

        const VFXNode* outSystem = findOutSystemNode();
        if (!outSystem)
            return false;

        std::vector<uint32_t> visited;
        return hasPathToOutSystem(*this, emitter->id, outSystem->id, visited);
    }

    std::string VFXGraph::getValidationError() const
    {
        const VFXNode* emitter = findEmitterNode();
        if (!emitter)
            return "Missing Emitter node";

        const VFXNode* outSystem = findOutSystemNode();
        if (!outSystem)
            return "Missing OutSystem node";

        std::vector<uint32_t> visited;
        if (hasPathToOutSystem(*this, emitter->id, outSystem->id, visited))
            return "";

        return "Emitter is not connected to OutSystem (directly or through modifiers/forces)";
    }
}
