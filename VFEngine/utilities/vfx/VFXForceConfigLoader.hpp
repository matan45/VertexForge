#pragma once

#include "VFXTypes.hpp"
#include "VFXForceTypes.hpp"
#include <vector>
#include <unordered_set>
#include <algorithm>

namespace vfx
{
    class VFXForceConfigLoader
    {
    public:
        // Extract ordered force chain from graph
        static VFXForceChain fromGraph(const VFXGraph& graph);

    private:
        // Build ordered list of force nodes from Emitter to OutSystem
        static std::vector<const VFXNode*> getForceNodeChain(const VFXGraph& graph);

        // Convert a force node to its runtime config
        static VFXForceConfig nodeToConfig(const VFXNode& node);

        // Individual extractors
        static GravityForceConfig extractGravityConfig(const VFXNode& node);
        static WindForceConfig extractWindConfig(const VFXNode& node);
        static TurbulenceForceConfig extractTurbulenceConfig(const VFXNode& node);
        static VortexForceConfig extractVortexConfig(const VFXNode& node);

        // Property extraction helpers
        static float getFloat(const VFXNode& node, const std::string& propName, float defaultValue);
        static int getInt(const VFXNode& node, const std::string& propName, int defaultValue);
        static bool getBool(const VFXNode& node, const std::string& propName, bool defaultValue);
        static glm::vec3 getVec3(const VFXNode& node, const std::string& propName, const glm::vec3& defaultValue);
    };

    inline VFXForceChain VFXForceConfigLoader::fromGraph(const VFXGraph& graph)
    {
        VFXForceChain chain;

        // Get ordered list of force nodes
        std::vector<const VFXNode*> forceNodes = getForceNodeChain(graph);

        // Convert each node to its config
        for (const VFXNode* node : forceNodes)
        {
            chain.forces.push_back(nodeToConfig(*node));
        }

        return chain;
    }

    inline std::vector<const VFXNode*> VFXForceConfigLoader::getForceNodeChain(const VFXGraph& graph)
    {
        std::vector<const VFXNode*> chain;

        // Start from Emitter node
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

        // Traverse from Emitter to OutSystem, collecting force nodes in order
        uint32_t currentNodeId = emitter->id;
        std::unordered_set<uint32_t> visited;  // O(1) lookup for cycle detection

        while (currentNodeId != outSystem->id)
        {
            // Prevent infinite loops - cycle detection
            if (visited.count(currentNodeId) > 0)
            {
                chain.clear();
                return chain;
            }
            visited.insert(currentNodeId);

            // Find outgoing link from current node (only follow "Input" links, not "Shape" links)
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

                    // If it's a force node, add to chain
                    if (isForceNode(targetNode->type))
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

    inline VFXForceConfig VFXForceConfigLoader::nodeToConfig(const VFXNode& node)
    {
        switch (node.type)
        {
        case VFXNodeType::ForceGravity:
            return extractGravityConfig(node);
        case VFXNodeType::ForceWind:
            return extractWindConfig(node);
        case VFXNodeType::ForceTurbulence:
            return extractTurbulenceConfig(node);
        case VFXNodeType::ForceVortex:
            return extractVortexConfig(node);
        default:
            // Return default gravity config for unknown types
            return GravityForceConfig{};
        }
    }

    inline GravityForceConfig VFXForceConfigLoader::extractGravityConfig(const VFXNode& node)
    {
        GravityForceConfig config;
        config.direction = getVec3(node, "direction", ForceDefaults::GRAVITY_DIRECTION);
        config.strength = getFloat(node, "strength", ForceDefaults::GRAVITY_STRENGTH);
        config.space = getBool(node, "localSpace", false) ? ForceSpace::Local : ForceSpace::World;
        return config;
    }

    inline WindForceConfig VFXForceConfigLoader::extractWindConfig(const VFXNode& node)
    {
        WindForceConfig config;
        config.direction = getVec3(node, "direction", ForceDefaults::WIND_DIRECTION);
        config.strength = getFloat(node, "strength", ForceDefaults::WIND_STRENGTH);
        config.noiseStrength = getFloat(node, "noiseStrength", ForceDefaults::WIND_NOISE_STRENGTH);
        config.noiseFrequency = getFloat(node, "noiseFrequency", ForceDefaults::WIND_NOISE_FREQUENCY);
        config.space = getBool(node, "localSpace", false) ? ForceSpace::Local : ForceSpace::World;
        return config;
    }

    inline TurbulenceForceConfig VFXForceConfigLoader::extractTurbulenceConfig(const VFXNode& node)
    {
        TurbulenceForceConfig config;
        config.strength = getFloat(node, "strength", ForceDefaults::TURBULENCE_STRENGTH);
        config.frequency = getFloat(node, "frequency", ForceDefaults::TURBULENCE_FREQUENCY);
        config.scrollSpeed = getFloat(node, "scrollSpeed", ForceDefaults::TURBULENCE_SCROLL_SPEED);
        config.octaves = std::clamp(getInt(node, "octaves", ForceDefaults::TURBULENCE_OCTAVES), 1, 4);
        config.space = getBool(node, "localSpace", false) ? ForceSpace::Local : ForceSpace::World;
        return config;
    }

    inline VortexForceConfig VFXForceConfigLoader::extractVortexConfig(const VFXNode& node)
    {
        VortexForceConfig config;
        config.axis = getVec3(node, "axis", ForceDefaults::VORTEX_AXIS);
        config.center = getVec3(node, "center", ForceDefaults::VORTEX_CENTER);
        config.strength = getFloat(node, "strength", ForceDefaults::VORTEX_STRENGTH);
        config.radialPull = getFloat(node, "radialPull", ForceDefaults::VORTEX_RADIAL_PULL);
        config.space = getBool(node, "localSpace", false) ? ForceSpace::Local : ForceSpace::World;
        return config;
    }

    inline float VFXForceConfigLoader::getFloat(const VFXNode& node, const std::string& propName, float defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<float>(&it->second.value))
            return *val;

        return defaultValue;
    }

    inline int VFXForceConfigLoader::getInt(const VFXNode& node, const std::string& propName, int defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<int32_t>(&it->second.value))
            return *val;

        return defaultValue;
    }

    inline bool VFXForceConfigLoader::getBool(const VFXNode& node, const std::string& propName, bool defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<bool>(&it->second.value))
            return *val;

        return defaultValue;
    }

    inline glm::vec3 VFXForceConfigLoader::getVec3(const VFXNode& node, const std::string& propName, const glm::vec3& defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<glm::vec3>(&it->second.value))
            return *val;

        return defaultValue;
    }
}
