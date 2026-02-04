#pragma once

#include "ISculptBrush.hpp"

namespace terrain::brushes
{
    class RaiseLowerBrush : public ISculptBrush
    {
    public:
        [[nodiscard]] BrushType getType() const override { return BrushType::Raise; }

        void apply(TerrainTile& tile, const BrushContext& context) override;
    };
}
