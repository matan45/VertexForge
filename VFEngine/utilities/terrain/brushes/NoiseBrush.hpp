#pragma once

#include "ISculptBrush.hpp"

namespace terrain::brushes
{
    class NoiseBrush : public ISculptBrush
    {
    public:
        [[nodiscard]] BrushType getType() const override { return BrushType::Noise; }

        void apply(TerrainTile& tile, const BrushContext& context) override;

    private:
        static float hashNoise(float x, float z);
    };
}
