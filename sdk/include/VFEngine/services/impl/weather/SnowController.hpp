#pragma once

#include "PrecipitationController.hpp"

namespace services
{
    class SnowController : public PrecipitationController
    {
    protected:
        PrecipitationConfig getConfig() const override;
    };
}
