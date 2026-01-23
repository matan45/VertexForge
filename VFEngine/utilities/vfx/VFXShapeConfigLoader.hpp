#pragma once

#include "VFXTypes.hpp"
#include "VFXShapeTypes.hpp"

namespace vfx
{
    class VFXShapeConfigLoader
    {
    public:
        static ShapeConfig fromGraph(const VFXGraph& graph);

    private:
        static const VFXNode* findShapeNode(const VFXGraph& graph);
        static ShapeConfig nodeToConfig(const VFXNode& node);

        static float getFloat(const VFXNode& node, const std::string& propName, float defaultValue);
        static bool getBool(const VFXNode& node, const std::string& propName, bool defaultValue);
        static glm::vec3 getVec3(const VFXNode& node, const std::string& propName, const glm::vec3& defaultValue);
        static std::string getString(const VFXNode& node, const std::string& propName, const std::string& defaultValue);
    };

    inline ShapeConfig VFXShapeConfigLoader::fromGraph(const VFXGraph& graph)
    {
        const VFXNode* shapeNode = findShapeNode(graph);

        if (!shapeNode)
        {
            ShapeConfig defaultConfig;
            defaultConfig.type = ShapeType::Point;
            return defaultConfig;
        }

        return nodeToConfig(*shapeNode);
    }

    inline const VFXNode* VFXShapeConfigLoader::findShapeNode(const VFXGraph& graph)
    {
        const VFXNode* emitter = graph.findEmitterNode();
        if (!emitter)
            return nullptr;

        for (const auto& link : graph.links)
        {
            if (link.targetNodeId == emitter->id && link.targetPin == "Shape")
            {
                const VFXNode* sourceNode = graph.findNode(link.sourceNodeId);
                if (sourceNode && isShapeNode(sourceNode->type))
                {
                    return sourceNode;
                }
            }
        }

        return nullptr;
    }

    inline ShapeConfig VFXShapeConfigLoader::nodeToConfig(const VFXNode& node)
    {
        ShapeConfig config;

        std::string shapeTypeStr = getString(node, "shapeType", "Point");
        config.type = stringToShapeType(shapeTypeStr);

        std::string emitFromStr = getString(node, "emitFrom", "Volume");
        config.emitFrom = stringToEmitFrom(emitFromStr);

        config.randomDirection = getBool(node, "randomDirection", false);

        switch (config.type)
        {
        case ShapeType::Point:
            config.dimensions = glm::vec4(0.0f);
            break;

        case ShapeType::Sphere:
            config.dimensions.x = getFloat(node, "radius", ShapeDefaults::SPHERE_RADIUS);
            break;

        case ShapeType::Cone:
            config.dimensions.x = getFloat(node, "radius", ShapeDefaults::CONE_BASE_RADIUS);
            config.dimensions.y = getFloat(node, "height", ShapeDefaults::CONE_HEIGHT);
            config.dimensions.z = getFloat(node, "angle", ShapeDefaults::CONE_ANGLE);
            break;

        case ShapeType::Box:
            {
                glm::vec3 halfExtents = getVec3(node, "halfExtents",
                    glm::vec3(ShapeDefaults::BOX_HALF_EXTENT_X,
                              ShapeDefaults::BOX_HALF_EXTENT_Y,
                              ShapeDefaults::BOX_HALF_EXTENT_Z));
                config.dimensions = glm::vec4(halfExtents, 0.0f);
            }
            break;

        case ShapeType::Torus:
            config.dimensions.x = getFloat(node, "majorRadius", ShapeDefaults::TORUS_MAJOR_RADIUS);
            config.dimensions.y = getFloat(node, "minorRadius", ShapeDefaults::TORUS_MINOR_RADIUS);
            break;
        }

        return config;
    }

    inline float VFXShapeConfigLoader::getFloat(const VFXNode& node, const std::string& propName, float defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<float>(&it->second.value))
            return *val;

        return defaultValue;
    }

    inline bool VFXShapeConfigLoader::getBool(const VFXNode& node, const std::string& propName, bool defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<bool>(&it->second.value))
            return *val;

        return defaultValue;
    }

    inline glm::vec3 VFXShapeConfigLoader::getVec3(const VFXNode& node, const std::string& propName, const glm::vec3& defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<glm::vec3>(&it->second.value))
            return *val;

        return defaultValue;
    }

    inline std::string VFXShapeConfigLoader::getString(const VFXNode& node, const std::string& propName, const std::string& defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<std::string>(&it->second.value))
            return *val;

        return defaultValue;
    }
}
