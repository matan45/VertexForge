#pragma once

#include "TerrainTypes.hpp"
#include "RoadProfile.hpp"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace terrain
{
    // VK-1621. What one spline apply does. This replaced an exclusive SplineMode{Sculpt, Paint}
    // enum: a road needs the corridor flattened, the layer painted AND the mesh generated, and
    // making the author draw the same spline three times is not a workflow. A bitmask also removes
    // the ordering ambiguity — sculpt runs first, so the mesh conforms to the deformed terrain.
    enum class SplineOps : uint8_t
    {
        None = 0,
        Sculpt = 1 << 0, // deform terrain height toward the curve
        Paint = 1 << 1,  // paint a material layer along the corridor
        Mesh = 1 << 2    // generate the road mesh asset + entities
    };

    [[nodiscard]] inline constexpr SplineOps operator|(SplineOps a, SplineOps b) noexcept
    {
        return static_cast<SplineOps>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
    }

    [[nodiscard]] inline constexpr SplineOps operator&(SplineOps a, SplineOps b) noexcept
    {
        return static_cast<SplineOps>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
    }

    inline constexpr SplineOps& operator|=(SplineOps& a, SplineOps b) noexcept
    {
        a = a | b;
        return a;
    }

    [[nodiscard]] inline constexpr bool hasOp(SplineOps ops, SplineOps op) noexcept
    {
        return (static_cast<uint8_t>(ops) & static_cast<uint8_t>(op)) != 0;
    }

    struct SplineControlPoint
    {
        glm::vec3 position{0.0f}; // world XYZ (Y = height)
    };

    struct SplineParams
    {
        SplineOps ops = SplineOps::Sculpt;
        float corridorWidth = 5.0f;    // flat area half-width
        float falloffWidth = 3.0f;     // smooth blend distance outside corridor
        float embankmentHeight = 0.0f; // raise above spline height (sculpt op)
        uint32_t paintLayer = 1;       // material layer index (paint op)

        // VK-1621 road mesh (Mesh op). `roadName` names both the generated .vfMesh and the scene
        // entity; `roadMaterialPath` is a .vfMat/.vfMatInstance applied to every chunk.
        // Defaulted to a real 4-column cross-section rather than an empty one: an empty column
        // list is not a valid profile, and a default-constructed SplineParams is what both the
        // service and the tool panel start from.
        RoadProfile road = makeDefaultRoadProfile();
        std::string roadName = "Road";
        std::string roadMaterialPath;
        bool roadCollider = false;
    };

    struct SplineData
    {
        uint64_t id = 0;
        std::vector<SplineControlPoint> controlPoints;
        SplineParams params;
        // VK-1645 removed `originalHeights`. It held a snapshot of the ALREADY-COMPOSITED tile
        // plane, so spline B's copy contained spline A's deformation: deleting A then B
        // resurrected A, and deleting B first wiped A. The height effect is a reserved layer
        // over an authoritative base now, and reverting it means dropping the layer and
        // recomposing -- parametric, order-independent, and with nothing per-spline to store.
    };
}
