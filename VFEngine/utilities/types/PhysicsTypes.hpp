#pragma once
#include <cstdint>

namespace types {

    enum class ColliderShape : uint8_t
    {
        Box = 0,
        Sphere = 1,
        Capsule = 2,
        ConvexMesh = 3,
        TriangleMesh = 4
    };

    enum class RigidBodyType : uint8_t
    {
        Static = 0,
        Dynamic = 1,
        Kinematic = 2
    };

}
