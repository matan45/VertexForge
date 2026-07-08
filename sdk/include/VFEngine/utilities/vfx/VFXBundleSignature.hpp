#pragma once

#include <string>

namespace vfx
{
    // VK-1483 — identifies the *heavy* GPU resources a composited-sequence preview step needs
    // (pipeline type + mesh + texture). The VFXSequence preview reuses a parked StepBundle across
    // seek/loop only when the requested step's signature matches the parked one, so the 64 MB
    // staging ring + mesh load + pipeline are created once per preview lifetime instead of per seek.
    //
    // Pure, header-only and CPU-only (no Vulkan) so it is the unit-testable seam for the reuse
    // decision — the graphics module owns the actual bundles, but the decision lives here.
    struct VFXBundleSignature
    {
        int renderMode = 0;      // controllers::VFXPreviewParams::renderMode (0=Billboard,3=Mesh,4=Ribbon)
        std::string meshPath;    // meaningful only for MeshParticle steps
        std::string texturePath;

        bool operator==(const VFXBundleSignature&) const = default;
    };

    // True when a parked bundle's resources can be reused as-is for a step wanting `wanted`.
    inline bool canReuseBundle(const VFXBundleSignature& parked, const VFXBundleSignature& wanted)
    {
        return parked == wanted;
    }
}
