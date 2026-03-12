#pragma once

#include "../../../services/providers/render/IBillboardRenderProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core::adapters
{
    class BillboardRenderAdapter : public services::IBillboardRenderProvider
    {
    private:
        controllers::OffScreen* offScreen = nullptr;
        bool billboardEnabled = true;
        float maxDistance = 500.0f;
    public:
        explicit BillboardRenderAdapter() = default;
        ~BillboardRenderAdapter() override = default;

        void setOffScreenController(controllers::OffScreen* controller)
        {
            offScreen = controller;
        }

        void setBillboardRenderingEnabled(bool enabled) override;
        bool isBillboardRenderingEnabled() const override;

        void setBillboardMaxDistance(float distance) override;

        services::BillboardRenderStats getBillboardStats() const override;
   
    };
}
