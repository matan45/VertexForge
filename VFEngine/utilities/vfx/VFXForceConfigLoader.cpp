#include "VFXForceConfigLoader.hpp"
#include <unordered_set>
#include <algorithm>

namespace vfx
{
    VFXForceChain VFXForceConfigLoader::fromGraph(const VFXGraph& graph)
    {
        VFXForceChain chain;

        std::vector<const VFXNode*> forceNodes = getForceNodeChain(graph);

        for (const VFXNode* node : forceNodes)
        {
            chain.forces.push_back(nodeToConfig(*node));
        }

        return chain;
    }

    std::vector<const VFXNode*> VFXForceConfigLoader::getForceNodeChain(const VFXGraph& graph)
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

    VFXForceConfig VFXForceConfigLoader::nodeToConfig(const VFXNode& node)
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
        case VFXNodeType::ForceDrag:
            return extractDragConfig(node);
        case VFXNodeType::ForcePointAttractor:
            return extractPointAttractorConfig(node);
        default:
            return GravityForceConfig{};
        }
    }

    GravityForceConfig VFXForceConfigLoader::extractGravityConfig(const VFXNode& node)
    {
        GravityForceConfig config;
        config.direction = getVec3(node, "direction", ForceDefaults::GRAVITY_DIRECTION);
        config.strength = getFloat(node, "strength", ForceDefaults::GRAVITY_STRENGTH);
        config.space = getBool(node, "localSpace", false) ? ForceSpace::Local : ForceSpace::World;
        return config;
    }

    WindForceConfig VFXForceConfigLoader::extractWindConfig(const VFXNode& node)
    {
        WindForceConfig config;
        config.direction = getVec3(node, "direction", ForceDefaults::WIND_DIRECTION);
        config.strength = getFloat(node, "strength", ForceDefaults::WIND_STRENGTH);
        config.noiseStrength = getFloat(node, "noiseStrength", ForceDefaults::WIND_NOISE_STRENGTH);
        config.noiseFrequency = getFloat(node, "noiseFrequency", ForceDefaults::WIND_NOISE_FREQUENCY);
        config.space = getBool(node, "localSpace", false) ? ForceSpace::Local : ForceSpace::World;
        return config;
    }

    TurbulenceForceConfig VFXForceConfigLoader::extractTurbulenceConfig(const VFXNode& node)
    {
        TurbulenceForceConfig config;
        config.strength = getFloat(node, "strength", ForceDefaults::TURBULENCE_STRENGTH);
        config.frequency = getFloat(node, "frequency", ForceDefaults::TURBULENCE_FREQUENCY);
        config.scrollSpeed = getFloat(node, "scrollSpeed", ForceDefaults::TURBULENCE_SCROLL_SPEED);
        config.octaves = std::clamp(getInt(node, "octaves", ForceDefaults::TURBULENCE_OCTAVES), 1, 4);
        config.space = getBool(node, "localSpace", false) ? ForceSpace::Local : ForceSpace::World;
        return config;
    }

    VortexForceConfig VFXForceConfigLoader::extractVortexConfig(const VFXNode& node)
    {
        VortexForceConfig config;
        config.axis = getVec3(node, "axis", ForceDefaults::VORTEX_AXIS);
        config.center = getVec3(node, "center", ForceDefaults::VORTEX_CENTER);
        config.strength = getFloat(node, "strength", ForceDefaults::VORTEX_STRENGTH);
        config.radialPull = getFloat(node, "radialPull", ForceDefaults::VORTEX_RADIAL_PULL);
        config.space = getBool(node, "localSpace", false) ? ForceSpace::Local : ForceSpace::World;
        return config;
    }

    DragForceConfig VFXForceConfigLoader::extractDragConfig(const VFXNode& node)
    {
        DragForceConfig config;
        config.linearCoeff = getFloat(node, "linearCoeff", ForceDefaults::DRAG_LINEAR_COEFF);
        config.quadraticCoeff = getFloat(node, "quadraticCoeff", ForceDefaults::DRAG_QUADRATIC_COEFF);
        config.space = getBool(node, "localSpace", false) ? ForceSpace::Local : ForceSpace::World;
        return config;
    }

    PointAttractorForceConfig VFXForceConfigLoader::extractPointAttractorConfig(const VFXNode& node)
    {
        PointAttractorForceConfig config;
        config.position = getVec3(node, "position", ForceDefaults::ATTRACTOR_POSITION);
        config.strength = getFloat(node, "strength", ForceDefaults::ATTRACTOR_STRENGTH);
        config.radius = getFloat(node, "radius", ForceDefaults::ATTRACTOR_RADIUS);
        config.falloff = getFloat(node, "falloff", ForceDefaults::ATTRACTOR_FALLOFF);
        config.killAtCenter = getBool(node, "killAtCenter", ForceDefaults::ATTRACTOR_KILL_AT_CENTER);
        config.space = getBool(node, "localSpace", false) ? ForceSpace::Local : ForceSpace::World;
        return config;
    }

    float VFXForceConfigLoader::getFloat(const VFXNode& node, const std::string& propName, float defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<float>(&it->second.value))
            return *val;

        return defaultValue;
    }

    int VFXForceConfigLoader::getInt(const VFXNode& node, const std::string& propName, int defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<int32_t>(&it->second.value))
            return *val;

        return defaultValue;
    }

    bool VFXForceConfigLoader::getBool(const VFXNode& node, const std::string& propName, bool defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<bool>(&it->second.value))
            return *val;

        return defaultValue;
    }

    glm::vec3 VFXForceConfigLoader::getVec3(const VFXNode& node, const std::string& propName, const glm::vec3& defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<glm::vec3>(&it->second.value))
            return *val;

        return defaultValue;
    }
}
