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
        Torus,
        Ring, // VK-1525: flat ring / arc / annulus (append only - never renumber)
        Line  // line segment along dims.xyz, centered (-dims -> +dims); random = along it. append only
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
        // Ring:   x = radius, y = thickness (annulus half-width, 0 = wire), z = arcSpan (radians), w = startAngle (radians)
        glm::vec4 dimensions{0.0f, 0.0f, 0.0f, 0.0f};

        // VK-1525: ordered / path-driven placement. When `ordered`, spawn position walks the shape
        // deterministically by emitter age over `sweepDuration` seconds instead of filling randomly, so
        // the shape "draws itself out" one particle after another. Off => legacy random placement.
        bool ordered = false;
        bool orderedLoop = false;   // false = one-shot ("form once"), true = loop (redraw each period)
        float sweepDuration = 1.0f; // seconds for the sweep to traverse the shape once
        float orderedJitter = 0.0f; // per-particle scatter off the on-curve point (world units, 0 = exact)
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
        inline constexpr float RING_RADIUS = 1.0f;
        inline constexpr float RING_THICKNESS = 0.1f;
        inline constexpr float RING_ARC = 6.28318530718f; // 2*pi (full ring)
        inline constexpr float RING_START_ANGLE = 0.0f;
        inline constexpr float LINE_HALF_X = 0.5f; // default line = 1 unit along local X (centered)
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
        case ShapeType::Ring:
            return glm::vec4(ShapeDefaults::RING_RADIUS, ShapeDefaults::RING_THICKNESS,
                             ShapeDefaults::RING_ARC, ShapeDefaults::RING_START_ANGLE);
        case ShapeType::Line:
            return glm::vec4(ShapeDefaults::LINE_HALF_X, 0.0f, 0.0f, 0.0f);
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
        case ShapeType::Ring:   return "Ring";
        case ShapeType::Line:   return "Line";
        default:                return "Point";
        }
    }

    inline ShapeType stringToShapeType(const std::string& str)
    {
        if (str == "Sphere") return ShapeType::Sphere;
        if (str == "Cone")   return ShapeType::Cone;
        if (str == "Box")    return ShapeType::Box;
        if (str == "Torus") return ShapeType::Torus;
        if (str == "Ring")   return ShapeType::Ring;
        if (str == "Line")   return ShapeType::Line;
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
