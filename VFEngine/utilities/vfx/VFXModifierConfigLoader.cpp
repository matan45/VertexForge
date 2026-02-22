#include "VFXModifierConfigLoader.hpp"
#include <unordered_set>

namespace vfx
{
    VFXModifierChain VFXModifierConfigLoader::fromGraph(const VFXGraph& graph)
    {
        VFXModifierChain chain;

        std::vector<const VFXNode*> modifierNodes = getModifierNodeChain(graph);

        for (const VFXNode* node : modifierNodes)
        {
            chain.modifiers.push_back(nodeToConfig(*node));
        }

        return chain;
    }

    std::vector<const VFXNode*> VFXModifierConfigLoader::getModifierNodeChain(const VFXGraph& graph)
    {
        std::vector<const VFXNode*> chain;

        const VFXNode* emitter = graph.findEmitterNode();
        if (!emitter)
        {
            return chain;
        }

        const VFXNode* outSystem = graph.findOutSystemNode();
        if (!outSystem)
        {
            return chain;
        }

        uint32_t currentNodeId = emitter->id;
        std::unordered_set<uint32_t> visited;

        while (currentNodeId != outSystem->id)
        {
            if (visited.count(currentNodeId) > 0)
            {
                chain.clear();
                return chain;
            }
            visited.insert(currentNodeId);

            bool foundNext = false;
            for (const auto& link : graph.links)
            {
                if (link.sourceNodeId == currentNodeId && link.targetPin != "Shape")
                {
                    const VFXNode* targetNode = graph.findNode(link.targetNodeId);
                    if (!targetNode)
                    {
                        chain.clear();
                        return chain;
                    }

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
            {
                chain.clear();
                return chain;
            }
        }

        return chain;
    }

    VFXModifierConfig VFXModifierConfigLoader::nodeToConfig(const VFXNode& node)
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
            return ColorOverLifetimeConfig{};
        }
    }

    ColorOverLifetimeConfig VFXModifierConfigLoader::extractColorConfig(const VFXNode& node)
    {
        ColorOverLifetimeConfig config;
        config.gradient = getGradient(node, "gradient",
            VFXGradient::fromStartEnd(ModifierDefaults::COLOR_START, ModifierDefaults::COLOR_END));
        return config;
    }

    SizeOverLifetimeConfig VFXModifierConfigLoader::extractSizeConfig(const VFXNode& node)
    {
        SizeOverLifetimeConfig config;
        config.curve = getCurve(node, "curve",
            VFXCurve::fromStartEnd(ModifierDefaults::SIZE_START_MULTIPLIER, ModifierDefaults::SIZE_END_MULTIPLIER));
        return config;
    }

    SpeedOverLifetimeConfig VFXModifierConfigLoader::extractSpeedConfig(const VFXNode& node)
    {
        SpeedOverLifetimeConfig config;
        config.curve = getCurve(node, "curve",
            VFXCurve::fromStartEnd(ModifierDefaults::SPEED_START_MULTIPLIER, ModifierDefaults::SPEED_END_MULTIPLIER));
        return config;
    }

    RotationOverLifetimeConfig VFXModifierConfigLoader::extractRotationConfig(const VFXNode& node)
    {
        RotationOverLifetimeConfig config;
        config.curve = getCurve(node, "curve", VFXCurve::constant(ModifierDefaults::ANGULAR_VELOCITY));
        return config;
    }

    float VFXModifierConfigLoader::getFloat(const VFXNode& node, const std::string& propName, float defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<float>(&it->second.value))
            return *val;

        return defaultValue;
    }

    glm::vec4 VFXModifierConfigLoader::getVec4(const VFXNode& node, const std::string& propName, const glm::vec4& defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<glm::vec4>(&it->second.value))
            return *val;

        return defaultValue;
    }

    VFXCurve VFXModifierConfigLoader::getCurve(const VFXNode& node, const std::string& propName, const VFXCurve& defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<VFXCurve>(&it->second.value))
            return *val;

        return defaultValue;
    }

    VFXGradient VFXModifierConfigLoader::getGradient(const VFXNode& node, const std::string& propName, const VFXGradient& defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<VFXGradient>(&it->second.value))
            return *val;

        return defaultValue;
    }
}
