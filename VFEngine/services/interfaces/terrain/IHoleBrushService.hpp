#pragma once
#include "../../utilities/terrain/HoleBrushTypes.hpp"

namespace services
{
    class IHoleBrushService
    {
    public:
        virtual ~IHoleBrushService() = default;

        virtual void registerEventHandlers() = 0;

        virtual void setParams(const terrain::HoleBrushParams& params) = 0;
        virtual void setRadius(float radius) = 0;
        virtual terrain::HoleBrushParams getParams() const = 0;
    };
}
