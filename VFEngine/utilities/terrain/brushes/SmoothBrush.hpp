#pragma once

#include "ISculptBrush.hpp"

namespace terrain::brushes
{
    class SmoothBrush : public ISculptBrush
    {
    public:
        [[nodiscard]] BrushType getType() const override { return BrushType::Smooth; }

        void apply(TerrainTile& tile, const BrushContext& context) override;
    };
}
