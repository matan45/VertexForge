#pragma once

#include "VFXTypes.hpp"
#include "VFXModifierTypes.hpp"
#include <vector>

namespace vfx
{
    class VFXModifierConfigLoader
    {
    public:
        // Extract ordered modifier chain from graph
        static VFXModifierChain fromGraph(const VFXGraph& graph);

    private:
        // Build ordered list of modifier nodes from Emitter to OutSystem
        static std::vector<const VFXNode*> getModifierNodeChain(const VFXGraph& graph);

        // Convert a modifier node to its runtime config
        static VFXModifierConfig nodeToConfig(const VFXNode& node);

        // Individual extractors
        static ColorOverLifetimeConfig extractColorConfig(const VFXNode& node);
        static SizeOverLifetimeConfig extractSizeConfig(const VFXNode& node);
        static SpeedOverLifetimeConfig extractSpeedConfig(const VFXNode& node);
        static RotationOverLifetimeConfig extractRotationConfig(const VFXNode& node);

        // Property extraction helpers
        static float getFloat(const VFXNode& node, const std::string& propName, float defaultValue);
        static glm::vec4 getVec4(const VFXNode& node, const std::string& propName, const glm::vec4& defaultValue);
    };

    inline VFXModifierChain VFXModifierConfigLoader::fromGraph(const VFXGraph& graph)
    {
        VFXModifierChain chain;

        // Get ordered list of modifier nodes
        std::vector<const VFXNode*> modifierNodes = getModifierNodeChain(graph);

        // Convert each node to its config
        for (const VFXNode* node : modifierNodes)
        {
            chain.modifiers.push_back(nodeToConfig(*node));
        }

        return chain;
    }

    inline std::vector<const VFXNode*> VFXModifierConfigLoader::getModifierNodeChain(const VFXGraph& graph)
    {
        std::vector<const VFXNode*> chain;

        // Start from Emitter node
        const VFXNode* emitter = graph.findEmitterNode();
        if (!emitter)
            return chain;

        const VFXNode* outSystem = graph.findOutSystemNode();
        if (!outSystem)
            return chain;

        // Traverse from Emitter to OutSystem, collecting modifier nodes in order
        uint32_t currentNodeId = emitter->id;
        std::vector<uint32_t> visited;

        while (currentNodeId != outSystem->id)
        {
            // Prevent infinite loops
            if (std::find(visited.begin(), visited.end(), currentNodeId) != visited.end())
                break;
            visited.push_back(currentNodeId);

            // Find outgoing link from current node
            bool foundNext = false;
            for (const auto& link : graph.links)
            {
                if (link.sourceNodeId == currentNodeId)
                {
                    const VFXNode* targetNode = graph.findNode(link.targetNodeId);
                    if (!targetNode)
                        break;

                    // If it's a modifier, add to chain and continue
                    if (isModifierNode(targetNode->type))
                    {
                        chain.push_back(targetNode);
                    }

                    currentNodeId = targetNode->id;
                    foundNext = true;
                    break;
                }
            }

            if (!foundNext)
                break;
        }

        return chain;
    }

    inline VFXModifierConfig VFXModifierConfigLoader::nodeToConfig(const VFXNode& node)
    {
        switch (node.type)
        {
        case VFXNodeType::ColorOverLifetime:
            return extractColorConfig(node);
        case VFXNodeType::SizeOverLifetime:
            return extractSizeConfig(node);
        case VFXNodeType::SpeedOverLifetime:
            return extractSpeedConfig(node);
        case VFXNodeType::RotationOverLifetime:
            return extractRotationConfig(node);
        default:
            // Return default color config for unknown types
            return ColorOverLifetimeConfig{};
        }
    }

    inline ColorOverLifetimeConfig VFXModifierConfigLoader::extractColorConfig(const VFXNode& node)
    {
        ColorOverLifetimeConfig config;
        config.startColor = getVec4(node, "startColor", ModifierDefaults::COLOR_START);
        config.endColor = getVec4(node, "endColor", ModifierDefaults::COLOR_END);
        return config;
    }

    inline SizeOverLifetimeConfig VFXModifierConfigLoader::extractSizeConfig(const VFXNode& node)
    {
        SizeOverLifetimeConfig config;
        config.startMultiplier = getFloat(node, "startMultiplier", ModifierDefaults::SIZE_START_MULTIPLIER);
        config.endMultiplier = getFloat(node, "endMultiplier", ModifierDefaults::SIZE_END_MULTIPLIER);
        return config;
    }

    inline SpeedOverLifetimeConfig VFXModifierConfigLoader::extractSpeedConfig(const VFXNode& node)
    {
        SpeedOverLifetimeConfig config;
        config.startMultiplier = getFloat(node, "startMultiplier", ModifierDefaults::SPEED_START_MULTIPLIER);
        config.endMultiplier = getFloat(node, "endMultiplier", ModifierDefaults::SPEED_END_MULTIPLIER);
        return config;
    }

    inline RotationOverLifetimeConfig VFXModifierConfigLoader::extractRotationConfig(const VFXNode& node)
    {
        RotationOverLifetimeConfig config;
        config.angularVelocity = getFloat(node, "angularVelocity", ModifierDefaults::ANGULAR_VELOCITY);
        return config;
    }

    inline float VFXModifierConfigLoader::getFloat(const VFXNode& node, const std::string& propName, float defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<float>(&it->second.value))
            return *val;

        return defaultValue;
    }

    inline glm::vec4 VFXModifierConfigLoader::getVec4(const VFXNode& node, const std::string& propName, const glm::vec4& defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<glm::vec4>(&it->second.value))
            return *val;

        return defaultValue;
    }
}
