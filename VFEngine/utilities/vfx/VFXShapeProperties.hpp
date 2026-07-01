#pragma once

#include "VFXShapeTypes.hpp"
#include "VFXTypes.hpp"

#include <string>

namespace vfx
{
    inline void applyShapeTypeProperties(VFXNode& node, ShapeType shapeType)
    {
        node.properties["shapeType"] = VFXProperty{
            "shapeType", VFXPropertyType::String,
            std::string(shapeTypeToString(shapeType)), 0.0f, 1.0f
        };

        node.properties.erase("radius");
        node.properties.erase("height");
        node.properties.erase("angle");
        node.properties.erase("halfExtents");
        node.properties.erase("majorRadius");
        node.properties.erase("minorRadius");

        switch (shapeType)
        {
        case ShapeType::Sphere:
            node.properties["radius"] = VFXProperty{
                "radius", VFXPropertyType::Float,
                ShapeDefaults::SPHERE_RADIUS, 0.01f, 100.0f
            };
            break;

        case ShapeType::Cone:
            node.properties["radius"] = VFXProperty{
                "radius", VFXPropertyType::Float,
                ShapeDefaults::CONE_BASE_RADIUS, 0.01f, 100.0f
            };
            node.properties["height"] = VFXProperty{
                "height", VFXPropertyType::Float,
                ShapeDefaults::CONE_HEIGHT, 0.01f, 100.0f
            };
            node.properties["angle"] = VFXProperty{
                "angle", VFXPropertyType::Float,
                ShapeDefaults::CONE_ANGLE, 0.0f, 1.57f
            };
            break;

        case ShapeType::Box:
            node.properties["halfExtents"] = VFXProperty{
                "halfExtents", VFXPropertyType::Vec3,
                glm::vec3(ShapeDefaults::BOX_HALF_EXTENT_X,
                          ShapeDefaults::BOX_HALF_EXTENT_Y,
                          ShapeDefaults::BOX_HALF_EXTENT_Z),
                0.01f, 100.0f
            };
            break;

        case ShapeType::Torus:
            node.properties["majorRadius"] = VFXProperty{
                "majorRadius", VFXPropertyType::Float,
                ShapeDefaults::TORUS_MAJOR_RADIUS, 0.01f, 100.0f
            };
            node.properties["minorRadius"] = VFXProperty{
                "minorRadius", VFXPropertyType::Float,
                ShapeDefaults::TORUS_MINOR_RADIUS, 0.01f, 50.0f
            };
            break;

        case ShapeType::Point:
        default:
            break;
        }
    }
}
