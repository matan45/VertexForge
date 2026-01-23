#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <string>

namespace vfx
{
    enum class ShapeType : uint8_t
    {
        Point = 0,
        Sphere,
        Cone,
        Box,
        Torus
    };

    enum class EmitFrom : uint8_t
    {
        Volume = 0,
        Surface
    };

    struct ShapeConfig
    {
        ShapeType type = ShapeType::Point;
        EmitFrom emitFrom = EmitFrom::Volume;
        bool randomDirection = false;

        // Dimensions interpretation by shape type:
        // Sphere: x = radius
        // Cone:   x = baseRadius, y = height, z = angle (radians)
        // Box:    xyz = halfExtents
        // Torus:  x = majorRadius (ring radius), y = minorRadius (tube radius)
        glm::vec4 dimensions{0.0f, 0.0f, 0.0f, 0.0f};
    };

    namespace ShapeDefaults
    {
        inline constexpr float SPHERE_RADIUS = 1.0f;
        inline constexpr float CONE_BASE_RADIUS = 1.0f;
        inline constexpr float CONE_HEIGHT = 2.0f;
        inline constexpr float CONE_ANGLE = 0.5236f;
        inline constexpr float BOX_HALF_EXTENT_X = 0.5f;
        inline constexpr float BOX_HALF_EXTENT_Y = 0.5f;
        inline constexpr float BOX_HALF_EXTENT_Z = 0.5f;
        inline constexpr float TORUS_MAJOR_RADIUS = 1.0f;
        inline constexpr float TORUS_MINOR_RADIUS = 0.25f;
    }

    inline glm::vec4 getDefaultDimensions(ShapeType type)
    {
        switch (type)
        {
        case ShapeType::Point:
            return glm::vec4(0.0f);
        case ShapeType::Sphere:
            return glm::vec4(ShapeDefaults::SPHERE_RADIUS, 0.0f, 0.0f, 0.0f);
        case ShapeType::Cone:
            return glm::vec4(ShapeDefaults::CONE_BASE_RADIUS, ShapeDefaults::CONE_HEIGHT, ShapeDefaults::CONE_ANGLE, 0.0f);
        case ShapeType::Box:
            return glm::vec4(ShapeDefaults::BOX_HALF_EXTENT_X, ShapeDefaults::BOX_HALF_EXTENT_Y, ShapeDefaults::BOX_HALF_EXTENT_Z, 0.0f);
        case ShapeType::Torus:
            return glm::vec4(ShapeDefaults::TORUS_MAJOR_RADIUS, ShapeDefaults::TORUS_MINOR_RADIUS, 0.0f, 0.0f);
        default:
            return glm::vec4(0.0f);
        }
    }

    inline const char* shapeTypeToString(ShapeType type)
    {
        switch (type)
        {
        case ShapeType::Point:  return "Point";
        case ShapeType::Sphere: return "Sphere";
        case ShapeType::Cone:   return "Cone";
        case ShapeType::Box:    return "Box";
        case ShapeType::Torus: return "Torus";
        default:                return "Point";
        }
    }

    inline ShapeType stringToShapeType(const std::string& str)
    {
        if (str == "Sphere") return ShapeType::Sphere;
        if (str == "Cone")   return ShapeType::Cone;
        if (str == "Box")    return ShapeType::Box;
        if (str == "Torus") return ShapeType::Torus;
        return ShapeType::Point;
    }

    inline const char* emitFromToString(EmitFrom mode)
    {
        switch (mode)
        {
        case EmitFrom::Volume:  return "Volume";
        case EmitFrom::Surface: return "Surface";
        default:                return "Volume";
        }
    }

    inline EmitFrom stringToEmitFrom(const std::string& str)
    {
        if (str == "Surface") return EmitFrom::Surface;
        return EmitFrom::Volume;
    }
}
