#include "VFXShapeConfigLoader.hpp"

namespace vfx
{
    ShapeConfig VFXShapeConfigLoader::fromGraph(const VFXGraph& graph)
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

    const VFXNode* VFXShapeConfigLoader::findShapeNode(const VFXGraph& graph)
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

    ShapeConfig VFXShapeConfigLoader::nodeToConfig(const VFXNode& node)
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

        case ShapeType::Ring:
            config.dimensions.x = getFloat(node, "radius", ShapeDefaults::RING_RADIUS);
            config.dimensions.y = getFloat(node, "thickness", ShapeDefaults::RING_THICKNESS);
            config.dimensions.z = getFloat(node, "arc", ShapeDefaults::RING_ARC);
            config.dimensions.w = getFloat(node, "startAngle", ShapeDefaults::RING_START_ANGLE);
            break;

        case ShapeType::Line:
            // dims.xyz = half-vector (center -> one end) in emitter-local space; spans -dims..+dims.
            config.dimensions = glm::vec4(
                getVec3(node, "lineHalf", glm::vec3(ShapeDefaults::LINE_HALF_X, 0.0f, 0.0f)), 0.0f);
            break;
        }

        // VK-1525: ordered / path-driven placement (all shapes). Missing keys => feature off, so
        // legacy .vfVFX assets load byte-identically (random placement).
        config.ordered = getBool(node, "ordered", false);
        config.orderedLoop = getBool(node, "orderedLoop", false);
        config.sweepDuration = getFloat(node, "sweepDuration", 1.0f);
        config.orderedJitter = getFloat(node, "orderedJitter", 0.0f);

        return config;
    }

    float VFXShapeConfigLoader::getFloat(const VFXNode& node, const std::string& propName, float defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<float>(&it->second.value))
            return *val;

        return defaultValue;
    }

    bool VFXShapeConfigLoader::getBool(const VFXNode& node, const std::string& propName, bool defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<bool>(&it->second.value))
            return *val;

        return defaultValue;
    }

    glm::vec3 VFXShapeConfigLoader::getVec3(const VFXNode& node, const std::string& propName, const glm::vec3& defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<glm::vec3>(&it->second.value))
            return *val;

        return defaultValue;
    }

    std::string VFXShapeConfigLoader::getString(const VFXNode& node, const std::string& propName, const std::string& defaultValue)
    {
        auto it = node.properties.find(propName);
        if (it == node.properties.end())
            return defaultValue;

        if (auto* val = std::get_if<std::string>(&it->second.value))
            return *val;

        return defaultValue;
    }
}
