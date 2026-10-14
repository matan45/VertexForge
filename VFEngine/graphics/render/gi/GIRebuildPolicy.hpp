#pragma once

#include "GITypes.hpp"

namespace render::gi
{
    // VK-1654: true when moving from `current` to `next` changes the probe-grid structure, so
    // GPUDrivenRenderer::applyGISettings must take its full rebuild path (cleanupGI waits for the
    // device to go idle, then initGI re-creates the cascades, pipelines and debug renderer and
    // re-arms the initial probe upload).
    //
    // RadianceCascadeManager::buildCascades reads cascadeMultiplier and the three far-field grid
    // fields besides quality and probeSpacing. Before this policy a far-field change reached
    // RadianceCascadeManager::applySettings, which destroyed the probe storage without waiting for
    // in-flight frames, and a cascadeMultiplier change was ignored until the next rebuild.
    // farFieldMaxDistance is deliberately absent: it is a per-dispatch push constant.
    //
    // GI owns no probe resources unless it is enabled at Medium or higher (GPUDrivenRenderer::initGI
    // returns early otherwise), so a change between two inactive states rebuilds nothing; the new
    // values are only cached and take effect when GI is turned on.
    [[nodiscard]] inline bool giSettingsRequireRebuild(const GISettings& current, const GISettings& next)
    {
        const auto isActive = [](const GISettings& s) { return s.enabled && s.quality != GIQuality::Off; };
        if (!isActive(current) && !isActive(next))
            return false;

        return next.quality != current.quality ||
               next.enabled != current.enabled ||
               next.probeSpacing != current.probeSpacing ||
               next.cascadeMultiplier != current.cascadeMultiplier ||
               next.farFieldEnabled != current.farFieldEnabled ||
               next.farFieldCascadeCount != current.farFieldCascadeCount ||
               next.farFieldProbeSpacing != current.farFieldProbeSpacing;
    }
}
