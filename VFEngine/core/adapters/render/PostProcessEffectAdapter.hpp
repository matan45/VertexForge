#pragma once
#include "../../services/providers/render/IPostProcessEffectProvider.hpp"

namespace controllers
{
    class OffScreen;
}

namespace core
{
    class PostProcessEffectAdapter : public services::IPostProcessEffectProvider
    {
    private:
        controllers::OffScreen* offScreen;

    public:
        explicit PostProcessEffectAdapter(controllers::OffScreen* offScreen);
        ~PostProcessEffectAdapter() override = default;

        plugin::PostProcessEffectHandle registerEffect(const plugin::PostProcessEffectDesc& desc) override;
        void updateEffectParams(plugin::PostProcessEffectHandle handle, std::vector<std::byte>&& params) override;
        void setEffectEnabled(plugin::PostProcessEffectHandle handle, bool enabled) override;
        void unregisterEffect(plugin::PostProcessEffectHandle handle) override;
    };
}
