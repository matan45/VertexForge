#pragma once

#include "../../../services/providers/render/IDecalRenderProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core::adapters
{
    class DecalRenderAdapter : public services::IDecalRenderProvider
    {
    private:
        controllers::OffScreen* offScreen = nullptr;
        bool decalEnabled = true;

    public:
        explicit DecalRenderAdapter() = default;
        ~DecalRenderAdapter() override = default;

        void setOffScreenController(controllers::OffScreen* controller)
        {
            offScreen = controller;
        }

        void setDecalRenderingEnabled(bool enabled) override;
        bool isDecalRenderingEnabled() const override;
    };
}
