#pragma once
// VK-1575: foliage brush parameters. Lives in Utilities so both the Services brush
// implementation and the Editor panel share one definition (mirrors meshbrush::MeshBrushParams).
// Per-type placement masks (slope/altitude) live on foliage::FoliageType; these are the
// brush-global geometry + mode controls.
#include <cstdint>
#include <algorithm>
#include "../terrain/BrushTypes.hpp"

namespace foliage
{
    enum class FoliageBrushMode : uint8_t
    {
        Paint = 0,
        Erase = 1
    };

    enum class FoliagePlacementMode : uint8_t
    {
        Spray  = 0, // continuous airbrush throttled by flowRate
        Single = 1  // one hero instance per click, gated by spacing
    };

    struct FoliageBrushParams
    {
        float radius         = 5.0f;
        float density        = 1.0f;
        float spacing        = 2.0f;
        float positionJitter = 0.5f;
        terrain::BrushFalloff falloff = terrain::BrushFalloff::Smooth;
        FoliagePlacementMode  placementMode = FoliagePlacementMode::Spray;
        float flowRate = 8.0f; // Spray: placements-per-second throttle (flowRate * deltaTime)
        // Brush tint applied to painted instances, packed 0xRRGGBBAA. 0xFFFFFFFF = no tint.
        uint32_t tint = 0xFFFFFFFFu;
        // When true, Erase only removes instances of the currently-selected palette type.
        bool eraseSelectedTypeOnly = false;

        void validate()
        {
            radius         = std::max(radius, 0.1f);
            density        = std::clamp(density, 0.01f, 10.0f);
            spacing        = std::max(spacing, 0.1f);
            positionJitter = std::clamp(positionJitter, 0.0f, 1.0f);
            flowRate       = std::clamp(flowRate, 0.1f, 100.0f);
        }
    };
}
