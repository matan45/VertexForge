#pragma once
#include "../../utilities/terrain/BrushTypes.hpp"

namespace services
{
    class IBrushService
    {
    public:
        virtual ~IBrushService() = default;

        virtual void registerEventHandlers() = 0;

        virtual void setParams(const terrain::BrushParams& params) = 0;
        virtual void setRadius(float radius) = 0;
        virtual void setStrength(float strength) = 0;
        virtual terrain::BrushParams getParams() const = 0;
    };
}
