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
        std::vector<services::DecalRenderData> currentDecals;

    public:
        explicit DecalRenderAdapter() = default;
        ~DecalRenderAdapter() override = default;

        void setOffScreenController(controllers::OffScreen* controller)
        {
            offScreen = controller;
        }

        void setDecalRenderingEnabled(bool enabled) override;
        bool isDecalRenderingEnabled() const override;

        void updateDecals(std::vector<services::DecalRenderData>&& decals) override;

        const std::vector<services::DecalRenderData>& getDecals() const { return currentDecals; }
    };
}
