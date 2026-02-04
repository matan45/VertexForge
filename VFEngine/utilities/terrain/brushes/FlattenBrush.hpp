#pragma once

#include "ISculptBrush.hpp"

namespace terrain::brushes
{
    class FlattenBrush : public ISculptBrush
    {
    public:
        [[nodiscard]] BrushType getType() const override { return BrushType::Flatten; }

        void apply(TerrainTile& tile, const BrushContext& context) override;
    };
}
