#pragma once

#include "../../interfaces/volumetric/IVolumetricNavService.hpp"
#include "../../providers/volumetric/IVolumetricNavProvider.hpp"
#include "../../events/volumetric/VolumetricNavEvents.hpp"
#include "VolumetricAgentManager.hpp"
#include <memory>

namespace services
{
    class VolumetricNavServiceImpl : public IVolumetricNavService
    {
    private:
        IVolumetricNavProvider* provider;
        VolumetricAgentManager agentManager;

    public:
        explicit VolumetricNavServiceImpl(IVolumetricNavProvider* provider);
        ~VolumetricNavServiceImpl() override = default;

        void registerEventHandlers() override;
    };
}
