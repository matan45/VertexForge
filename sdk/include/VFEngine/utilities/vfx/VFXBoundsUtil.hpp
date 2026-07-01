#pragma once

// VK-1453 (VFXSequence Phase 4) — explicit effect bounds helpers.
//
// The VFXBounds / VFXBoundsMode types live in VFXTypes.hpp (so VFXData can embed a
// bounds member without a circular include). This header adds the CPU-side helpers:
// an analytic Auto-bounds derivation that mirrors the runtime frustum-cull heuristic,
// and a resolve() that turns stored bounds into a concrete local-space AABB. Feeds the
// pre-spawn cull and the editor bounds visualization; no render/GPU types touched.

#include "VFXTypes.hpp"
#include "VFXShapeConfigLoader.hpp"
#include "../math/Frustum.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace vfx
{
    namespace boundsDetail
    {
        inline float getFloatProp(const VFXNode& node, const std::string& name, float def)
        {
            auto it = node.properties.find(name);
            if (it == node.properties.end())
                return def;
            if (const float* v = std::get_if<float>(&it->second.value))
                return *v;
            return def;
        }
    }

    // Analytic local-space AABB for a single .vfVFX, matching the conservative
    // sphere-radius heuristic in VFXSceneRenderer::isEmitterInFrustum:
    //   radius = max(lifetime * startSpeed, maxShapeDim) + 5, centered on the
    // emitter origin. Pure function of VFXData (no render/GPU types touched).
    inline math::AABB computeAutoBounds(const VFXData& data)
    {
        const VFXNode* emitter = data.graph.findEmitterNode();
        const float lifetime = emitter
            ? boundsDetail::getFloatProp(*emitter, "lifetime", EmitterDefaults::LIFETIME)
            : EmitterDefaults::LIFETIME;
        const float startSpeed = emitter
            ? boundsDetail::getFloatProp(*emitter, "startSpeed", EmitterDefaults::START_SPEED)
            : EmitterDefaults::START_SPEED;

        const ShapeConfig shape = VFXShapeConfigLoader::fromGraph(data.graph);
        const float maxDim = std::max({std::abs(shape.dimensions.x),
                                       std::abs(shape.dimensions.y),
                                       std::abs(shape.dimensions.z)});

        const float radius = std::max(lifetime * startSpeed, maxDim) + 5.0f;
        const glm::vec3 r{radius, radius, radius};
        return math::AABB(-r, r);
    }

    // Resolve an effect's bounds to a concrete local-space AABB. Fixed uses the
    // authored center/extents; Auto (or a degenerate zero-extent Fixed) derives
    // from the graph so a mis-authored box never yields an empty volume.
    inline math::AABB resolveBounds(const VFXBounds& bounds, const VFXData& data)
    {
        if (bounds.mode == VFXBoundsMode::Fixed &&
            (bounds.extents.x > 0.0f || bounds.extents.y > 0.0f || bounds.extents.z > 0.0f))
        {
            return math::AABB(bounds.center - bounds.extents, bounds.center + bounds.extents);
        }
        return computeAutoBounds(data);
    }
}
