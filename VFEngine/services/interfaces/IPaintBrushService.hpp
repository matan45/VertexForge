#pragma once
#include "../../utilities/terrain/PaintBrushTypes.hpp"

namespace services
{
    class IPaintBrushService
    {
    public:
        virtual ~IPaintBrushService() = default;

        virtual void registerEventHandlers() = 0;

        virtual void setParams(const terrain::PaintBrushParams& params) = 0;
        virtual void setRadius(float radius) = 0;
        virtual void setStrength(float strength) = 0;
        virtual void setOpacity(float opacity) = 0;
        virtual void setActiveLayer(uint32_t layer) = 0;
        virtual terrain::PaintBrushParams getParams() const = 0;

        virtual void setBrushType(terrain::PaintBrushType type) = 0;
        virtual terrain::PaintBrushType getBrushType() const = 0;
    };
}
