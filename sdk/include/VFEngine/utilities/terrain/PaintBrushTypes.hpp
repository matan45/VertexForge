#pragma once

#include "BrushTypes.hpp"
#include "SurfaceMaskBrushApplicator.hpp" // VK-1614: terrain::PaintTarget
#include <cstdint>
#include <algorithm>

namespace terrain
{
    enum class PaintBrushType : uint8_t
    {
        PaintLayer = 0,
        EraseLayer = 1,
        SmoothWeights = 2,
        FillLayer = 3,
        SetBaseLayer = 4
    };

    struct PaintBrushParams
    {
        float radius = 5.0f;
        float strength = 10.0f;
        float opacity = 1.0f;
        uint32_t activeLayer = 0;
        BrushFalloff falloff = BrushFalloff::Smooth;
        BrushShape shape = BrushShape::Circle;
        // VK-1614: what this stroke writes. Modelled as a brush PARAMETER, not as a ninth terrain
        // tool mode, because VertexForge already separates mode (PaintModeServiceImpl holds only
        // active + target entity) from parameters (this struct) — and because the mode-exclusivity
        // mesh is O(N^2) hand-wiring across seven peer services that is already missing edges.
        // Layers keeps the pre-existing weight-map behaviour; Wetness/Snow write the surface mask.
        PaintTarget target = PaintTarget::Layers;

        void validate()
        {
            radius = std::max(radius, 0.1f);
            strength = std::clamp(strength, 0.0f, 100.0f);
            opacity = std::clamp(opacity, 0.0f, 1.0f);
        }
    };
}
