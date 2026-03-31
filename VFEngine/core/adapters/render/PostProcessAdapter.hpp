#pragma once
#include "../../services/providers/render/IPostProcessProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core
{
    class PostProcessAdapter : public services::IPostProcessProvider
    {
    private:
        controllers::OffScreen* offScreen;

    public:
        explicit PostProcessAdapter(controllers::OffScreen* offScreen);
        ~PostProcessAdapter() override = default;

        void applyPostProcessSettings(const postprocess::PostProcessSettings& settings) override;
        postprocess::PostProcessSettings getPostProcessSettings() const override;
        void setPostProcessEnabled(bool enabled) override;
        bool isPostProcessEnabled() const override;
        events::postprocess::UpscaleStatus getUpscaleStatus() const override;
    };
}
