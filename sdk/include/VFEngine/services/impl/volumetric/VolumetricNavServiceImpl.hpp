#pragma once

#include "../../interfaces/volumetric/IVolumetricNavService.hpp"
#include "../../providers/volumetric/IVolumetricNavProvider.hpp"
#include "../../events/volumetric/VolumetricNavEvents.hpp"
#include "VolumetricAgentManager.hpp"
#include <memory>
#include <functional>

namespace services
{
    class VolumetricNavServiceImpl : public IVolumetricNavService
    {
    private:
        IVolumetricNavProvider* provider;
        VolumetricAgentManager agentManager;
        std::function<bool(glm::vec3, glm::vec3, float)> physicsRaycast; // origin, dir, maxDist → hit?

    public:
        VolumetricNavServiceImpl(IVolumetricNavProvider* provider,
                                  std::function<bool(glm::vec3, glm::vec3, float)> raycastFn = nullptr);
        ~VolumetricNavServiceImpl() override = default;

        void registerEventHandlers() override;
    };
}
