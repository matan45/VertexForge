#pragma once
#include "../../data/PostProcessEffectTypes.hpp"
#include <cstddef>
#include <vector>

namespace services {

    class IPostProcessEffectProvider {
    public:
        virtual ~IPostProcessEffectProvider() = default;

        virtual plugin::PostProcessEffectHandle registerEffect(const plugin::PostProcessEffectDesc& desc) = 0;

        virtual void updateEffectParams(plugin::PostProcessEffectHandle handle,
                                        std::vector<std::byte>&& params) = 0;

        virtual void setEffectEnabled(plugin::PostProcessEffectHandle handle, bool enabled) = 0;

        virtual void unregisterEffect(plugin::PostProcessEffectHandle handle) = 0;
    };

}
