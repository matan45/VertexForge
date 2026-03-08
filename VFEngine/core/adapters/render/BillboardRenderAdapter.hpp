#pragma once

#include "../../../services/providers/render/IBillboardRenderProvider.hpp"

namespace controllers
{
    class OffScreenController;
}

namespace core::adapters
{
    class BillboardRenderAdapter : public services::IBillboardRenderProvider
    {
    public:
        BillboardRenderAdapter() = default;
        ~BillboardRenderAdapter() override = default;

        void setOffScreenController(controllers::OffScreenController* controller)
        {
            offScreen = controller;
        }

        void setBillboardRenderingEnabled(bool enabled) override;
        bool isBillboardRenderingEnabled() const override;

        void setBillboardMaxDistance(float distance) override;

        services::BillboardRenderStats getBillboardStats() const override;

        services::ImposterBakeResult bakeImposter(const std::string& meshPath,
                                                    const std::string& outputPath) override;

    private:
        controllers::OffScreenController* offScreen = nullptr;
        bool billboardEnabled = true;
        float maxDistance = 500.0f;
    };
}
