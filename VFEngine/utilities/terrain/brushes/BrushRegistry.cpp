#include "BrushRegistry.hpp"
#include "RaiseLowerBrush.hpp"
#include "SmoothBrush.hpp"
#include "FlattenBrush.hpp"
#include "NoiseBrush.hpp"

namespace terrain::brushes
{
    BrushRegistry::BrushRegistry()
    {
        brushes[BrushType::Raise] = std::make_unique<RaiseLowerBrush>();
        brushes[BrushType::Lower] = std::make_unique<RaiseLowerBrush>();
        brushes[BrushType::Smooth] = std::make_unique<SmoothBrush>();
        brushes[BrushType::Flatten] = std::make_unique<FlattenBrush>();
        brushes[BrushType::Noise] = std::make_unique<NoiseBrush>();
    }

    ISculptBrush* BrushRegistry::getBrush(BrushType type) const
    {
        auto it = brushes.find(type);
        if (it != brushes.end())
        {
            return it->second.get();
        }
        return nullptr;
    }
}
