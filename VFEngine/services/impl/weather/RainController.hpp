#pragma once

#include "PrecipitationController.hpp"

namespace services
{
    class RainController : public PrecipitationController
    {
    protected:
        PrecipitationConfig getConfig() const override;
    };
}
