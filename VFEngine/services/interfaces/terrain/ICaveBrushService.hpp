#pragma once
#include "../../utilities/terrain/CaveBrushTypes.hpp"

namespace services
{
    class ICaveBrushService
    {
    public:
        virtual ~ICaveBrushService() = default;

        virtual void registerEventHandlers() = 0;

        virtual void setParams(const terrain::CaveBrushParams& params) = 0;
        virtual void setRadius(float radius) = 0;
        virtual void setStrength(float strength) = 0;
        virtual void setBrushType(terrain::CaveBrushType type) = 0;
        virtual terrain::CaveBrushParams getParams() const = 0;
        virtual terrain::CaveBrushType getBrushType() const = 0;
    };
}
