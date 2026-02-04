#pragma once

#include "ISculptBrush.hpp"
#include <unordered_map>
#include <memory>

namespace terrain::brushes
{
    class BrushRegistry
    {
    private:
        std::unordered_map<BrushType, std::unique_ptr<ISculptBrush>> brushes;

    public:
        BrushRegistry();

        [[nodiscard]] ISculptBrush* getBrush(BrushType type) const;
    };
}
